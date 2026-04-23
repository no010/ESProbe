/**
 * @file DAP_handle.c
 * @brief Handle DAP packets and transaction push
 * @version 0.5
 * @change: 2020.02.04 first version
 *          2020.11.11 support WinUSB mode
 *          2021.02.17 support SWO
 *          2021.10.03 try to handle unlink behavior
 *
 * @copyright Copyright (c) 2021
 *
 */

#include <stdint.h>
#include <string.h>

#include "main/usbip_server.h"
#include "main/DAP_handle.h"
#include "main/dap_configuration.h"
#include "main/wifi_configuration.h"

#include "components/USBIP/usb_descriptor.h"
#include "components/DAP/include/DAP.h"
#include "components/DAP/include/swo.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#if ((USE_MDNS == 1) || (USE_OTA == 1))
    #define DAP_BUFFER_NUM 10
#else
    #define DAP_BUFFER_NUM 20
#endif

#if (USE_WINUSB == 1)
typedef struct
{
    uint32_t length;
    uint8_t buf[DAP_PACKET_SIZE];
} DapPacket_t;
#else
typedef struct
{
    uint8_t buf[DAP_PACKET_SIZE];
} DapPacket_t;
#endif

#define DAP_HANDLE_SIZE (sizeof(DapPacket_t))


extern int kSock;
extern TaskHandle_t kDAPTaskHandle;

int kRestartDAPHandle = NO_SIGNAL;


static DapPacket_t DAPDataProcessed;
static int dap_respond = 0;
// Queue for saved IN URB headers.  pyocd may have multiple IN URBs in flight
// (start_rx() submits the next IN before the previous one completes).  A FIFO
// queue lets us pair each OUT response with the oldest waiting IN instead of
// dropping extras with empty ACKs that pollute pyocd's rcv_data queue.
#define MAX_PENDING_IN 4
static usbip_stage2_header pending_in_queue[MAX_PENDING_IN];
static int pending_in_count = 0;

// SWO Trace
static uint8_t *swo_data_to_send = NULL;
static uint32_t swo_data_num;

// DAP handle
static RingbufHandle_t dap_dataIN_handle = NULL;
static RingbufHandle_t dap_dataOUT_handle = NULL;
static SemaphoreHandle_t data_response_mux = NULL;

// Mutex to ensure DAP_ProcessCommand is never called concurrently from
// tcp_server (synchronous path) and DAP_Task (asynchronous path).
static SemaphoreHandle_t dap_cmd_mux = NULL;

void save_in_header(const usbip_stage2_header *header)
{
    if (pending_in_count < MAX_PENDING_IN) {
        memcpy(&pending_in_queue[pending_in_count], header, sizeof(usbip_stage2_header));
        pending_in_count++;
        os_printf("IN queued seq=%lu count=%d\r\n",
                  (unsigned long)ntohl(header->base.seqnum), pending_in_count);
    } else {
        os_printf("IN dropped seq=%lu queue full\r\n",
                  (unsigned long)ntohl(header->base.seqnum));
    }
}

int peek_oldest_in_header(usbip_stage2_header *header)
{
    if (pending_in_count > 0) {
        memcpy(header, &pending_in_queue[0], sizeof(usbip_stage2_header));
        return 1;
    }
    return 0;
}

int get_oldest_in_header(usbip_stage2_header *header)
{
    if (pending_in_count > 0) {
        memcpy(header, &pending_in_queue[0], sizeof(usbip_stage2_header));
        for (int i = 1; i < pending_in_count; i++) {
            memcpy(&pending_in_queue[i-1], &pending_in_queue[i], sizeof(usbip_stage2_header));
        }
        pending_in_count--;
        return 1;
    }
    return 0;
}

void remove_in_header_by_seqnum(uint32_t seqnum)
{
    for (int i = 0; i < pending_in_count; i++) {
        if (ntohl(pending_in_queue[i].base.seqnum) == seqnum) {
            for (int j = i + 1; j < pending_in_count; j++) {
                memcpy(&pending_in_queue[j-1], &pending_in_queue[j], sizeof(usbip_stage2_header));
            }
            pending_in_count--;
            os_printf("IN removed seq=%lu count=%d\r\n",
                      (unsigned long)seqnum, pending_in_count);
            return;
        }
    }
}

int has_pending_in(void)
{
    return pending_in_count > 0;
}

void malloc_dap_ringbuf() {
    if (data_response_mux && xSemaphoreTake(data_response_mux, portMAX_DELAY) == pdTRUE)
    {
        if (dap_dataIN_handle == NULL) {
            dap_dataIN_handle = xRingbufferCreate(DAP_HANDLE_SIZE * DAP_BUFFER_NUM, RINGBUF_TYPE_BYTEBUF);
        }
        if (dap_dataOUT_handle == NULL) {
            dap_dataOUT_handle = xRingbufferCreate(DAP_HANDLE_SIZE * DAP_BUFFER_NUM, RINGBUF_TYPE_BYTEBUF);
        }

        xSemaphoreGive(data_response_mux);
    }
}

void free_dap_ringbuf() {
    if (data_response_mux && xSemaphoreTake(data_response_mux, portMAX_DELAY) == pdTRUE) {
        if (dap_dataIN_handle) {
            vRingbufferDelete(dap_dataIN_handle);
        }
        if (dap_dataOUT_handle) {
            vRingbufferDelete(dap_dataOUT_handle);
        }

        dap_dataIN_handle = dap_dataOUT_handle = NULL;
        xSemaphoreGive(data_response_mux);
    }

}


void handle_dap_data_request(usbip_stage2_header *header, uint32_t length)
{
    uint8_t *data_in = (uint8_t *)header;
    data_in = &(data_in[sizeof(usbip_stage2_header)]);
    // Point to the beginning of the URB packet

    // Attempt synchronous processing to cut latency.  If DAP_Task currently
    // holds the mutex we fall back to the original asynchronous path.
    if (dap_cmd_mux != NULL &&
        xSemaphoreTake(dap_cmd_mux, pdMS_TO_TICKS(1)) == pdTRUE)
    {
        uint8_t buf[DAP_PACKET_SIZE];
        uint32_t packet_length = (length < DAP_PACKET_SIZE) ? length : DAP_PACKET_SIZE;
        memcpy(buf, data_in, packet_length);
        if (packet_length < DAP_PACKET_SIZE) {
            memset(buf + packet_length, 0, DAP_PACKET_SIZE - packet_length);
        }

        if (buf[0] == ID_DAP_QueueCommands) {
            buf[0] = ID_DAP_ExecuteCommands;
        }

        DapPacket_t response;
        memset(response.buf, 0, DAP_PACKET_SIZE);
        int resLength = DAP_ProcessCommand(buf, response.buf);
        resLength &= 0xFFFF;

        os_printf("DAP sync cmd=0x%02X, TX len=%d\r\n", buf[0], (int)resLength);

#if (USE_WINUSB == 1)
        response.length = resLength;
#endif

        // If an IN arrived before this OUT, reply to that saved IN *before*
        // acknowledging the OUT.  This gives the response a head-start on the
        // network, so it is more likely to be in pyocd's queue when read() is
        // called.
        if (pending_in_count > 0) {
            usbip_stage2_header saved_header;
            if (get_oldest_in_header(&saved_header)) {
                uint8_t send_buf[sizeof(usbip_stage2_header) + DAP_PACKET_SIZE];
                memcpy(send_buf, &saved_header, sizeof(usbip_stage2_header));
                send_stage2_submit_data_fast((usbip_stage2_header *)send_buf, response.buf,
#if (USE_WINUSB == 1)
                                             response.length
#else
                                             DAP_HANDLE_SIZE
#endif
                                            );
                os_printf("sync reply to saved IN seq=%lu\r\n",
                          (unsigned long)ntohl(saved_header.base.seqnum));
            }
        } else {
            xRingbufferSend(dap_dataOUT_handle, &response, DAP_HANDLE_SIZE, portMAX_DELAY);
            if (xSemaphoreTake(data_response_mux, portMAX_DELAY) == pdTRUE) {
                ++dap_respond;
                xSemaphoreGive(data_response_mux);
            }
        }

        xSemaphoreGive(dap_cmd_mux);

        // OUT ACK is sent after the IN response so the IN data hits the wire first.
        send_stage2_submit_data_fast(header, NULL, 0);
        return;
    }

    // Async path (fallback when DAP_Task is busy)
#if (USE_WINUSB == 1)
    send_stage2_submit_data_fast(header, NULL, 0);

    DapPacket_t packet;
    packet.length = (length < DAP_PACKET_SIZE) ? length : DAP_PACKET_SIZE;
    memcpy(packet.buf, data_in, packet.length);
    if (packet.length < DAP_PACKET_SIZE) {
        memset(packet.buf + packet.length, 0, DAP_PACKET_SIZE - packet.length);
    }
    xRingbufferSend(dap_dataIN_handle, &packet, DAP_HANDLE_SIZE, portMAX_DELAY);
    xTaskNotifyGive(kDAPTaskHandle);

#else
    send_stage2_submit_data_fast(header, NULL, 0);

    xRingbufferSend(dap_dataIN_handle, data_in, DAP_HANDLE_SIZE, portMAX_DELAY);
    xTaskNotifyGive(kDAPTaskHandle);

#endif
}

void handle_swo_trace_response(usbip_stage2_header *header)
{
#if (SWO_FUNCTION_ENABLE == 1)
    if (kSwoTransferBusy)
    {
        // busy indicates that there is data to be send
        os_printf("swo use data\r\n");
        send_stage2_submit_data(header, 0, (void *)swo_data_to_send, swo_data_num);
        SWO_TransferComplete();
    }
    else
    {
        // nothing to send.
        send_stage2_submit(header, 0, 0);
    }
#else
    send_stage2_submit(header, 0, 0);
#endif
}

// SWO Data Queue Transfer
//   buf:    pointer to buffer with data
//   num:    number of bytes to transfer
void SWO_QueueTransfer(uint8_t *buf, uint32_t num)
{
    swo_data_to_send = buf;
    swo_data_num = num;
}

void DAP_Thread(void *argument)
{
    dap_dataIN_handle = xRingbufferCreate(DAP_HANDLE_SIZE * DAP_BUFFER_NUM, RINGBUF_TYPE_BYTEBUF);
    dap_dataOUT_handle = xRingbufferCreate(DAP_HANDLE_SIZE * DAP_BUFFER_NUM, RINGBUF_TYPE_BYTEBUF);
    data_response_mux = xSemaphoreCreateMutex();
    dap_cmd_mux = xSemaphoreCreateMutex();
    size_t packetSize;
    int resLength;
    DapPacket_t *item;

    if (dap_dataIN_handle == NULL || dap_dataOUT_handle == NULL ||
        data_response_mux == NULL || dap_cmd_mux == NULL)
    {
        os_printf("Can not create DAP ringbuf/mux!\r\n");
        vTaskDelete(NULL);
    }
    for (;;)
    {

        while (1)
        {
            if (kRestartDAPHandle)
            {
                free_dap_ringbuf();

                if (kRestartDAPHandle == RESET_HANDLE) {
                    malloc_dap_ringbuf();
                    if (dap_dataIN_handle == NULL || dap_dataOUT_handle == NULL ||
                        dap_cmd_mux == NULL)
                    {
                        os_printf("Can not create DAP ringbuf/mux!\r\n");
                        vTaskDelete(NULL);
                    }
                }

                kRestartDAPHandle = NO_SIGNAL;
            }

            ulTaskNotifyTake(pdFALSE, portMAX_DELAY); // wait event


            if (dap_dataIN_handle == NULL || dap_dataOUT_handle == NULL) {
                continue; // may be use elaphureLink, wait...
            }

            // Synchronous processing in tcp_server may hold this mutex for a
            // few milliseconds.  Block until it is released.
            if (xSemaphoreTake(dap_cmd_mux, portMAX_DELAY) != pdTRUE) {
                continue;
            }

            packetSize = 0;
            item = (DapPacket_t *)xRingbufferReceiveUpTo(dap_dataIN_handle, &packetSize,
                                                         pdMS_TO_TICKS(1), DAP_HANDLE_SIZE);
            if (packetSize == 0)
            {
                xSemaphoreGive(dap_cmd_mux);
                break;
            }

            else if (packetSize < DAP_HANDLE_SIZE)
            {
                os_printf("Wrong data in packet size:%d , data in remain: %d\r\n", packetSize, (int)xRingbufferGetMaxItemSize(dap_dataIN_handle));
                vRingbufferReturnItem(dap_dataIN_handle, (void *)item);
                xSemaphoreGive(dap_cmd_mux);
                break;
                // This may not happen because there is a semaphore acquisition
            }

            if (item->buf[0] == ID_DAP_QueueCommands)
            {
                item->buf[0] = ID_DAP_ExecuteCommands;
            }

            memset(DAPDataProcessed.buf, 0, DAP_PACKET_SIZE);
            resLength = DAP_ProcessCommand((uint8_t *)item->buf, (uint8_t *)DAPDataProcessed.buf); // use first 4 byte to save length
            resLength &= 0xFFFF;                                                                   // res length in lower 16 bits
            if (item->buf[0] == ID_DAP_Info) {
                os_printf("DAP_Info id=%d, len=%d\r\n", item->buf[1], (int)resLength);
            } else {
                os_printf("DAP RX cmd=0x%02X, TX len=%d\r\n", item->buf[0], (int)resLength);
            }

            vRingbufferReturnItem(dap_dataIN_handle, (void *)item); // process done.

            // now prepare to reply
#if (USE_WINUSB == 1)
            DAPDataProcessed.length = resLength;
#endif
            xRingbufferSend(dap_dataOUT_handle, (void *)&DAPDataProcessed, DAP_HANDLE_SIZE, portMAX_DELAY);

            if (xSemaphoreTake(data_response_mux, portMAX_DELAY) == pdTRUE)
            {
                ++dap_respond;
                xSemaphoreGive(data_response_mux);
            }

            xSemaphoreGive(dap_cmd_mux);
        }
    }
}

int fast_reply(uint8_t *buf, uint32_t length, int dap_req_num)
{
    os_printf("fast_reply enter dap_req_num=%d dap_respond=%d pending=%d\r\n",
              dap_req_num, dap_respond, has_pending_in());
    if (dap_req_num > 0) {
        DapPacket_t *item;
        size_t packetSize = 0;
        item = (DapPacket_t *)xRingbufferReceiveUpTo(dap_dataOUT_handle, &packetSize,
                                                     portMAX_DELAY, DAP_HANDLE_SIZE);
        os_printf("fast_reply ringbuf packetSize=%d\r\n", (int)packetSize);
        if (packetSize == DAP_HANDLE_SIZE) {
#if (USE_WINUSB == 1)
            // Use actual DAP response length (item->length) to avoid sending trailing zeros.
            // This matches the original upstream behaviour and prevents pyocd from reading
            // padded zeros that could be misinterpreted as extra response data.
            send_stage2_submit_data_fast((usbip_stage2_header *)buf, item->buf, item->length);
#else
            send_stage2_submit_data_fast((usbip_stage2_header *)buf, item->buf, DAP_HANDLE_SIZE);
#endif

            if (xSemaphoreTake(data_response_mux, portMAX_DELAY) == pdTRUE) {
                --dap_respond;
                xSemaphoreGive(data_response_mux);
            }

            vRingbufferReturnItem(dap_dataOUT_handle, (void *)item);
            return 1;
        } else if (packetSize > 0) {
            os_printf("Wrong data out packet size:%d!\r\n", packetSize);
        }
        ////TODO: fast reply
    } else {
        // No OUT command pending yet: send an empty RET_SUBMIT so the host
        // knows the endpoint is alive.  pyocd pre-queues IN URBs and expects
        // either data or an immediate empty reply; without this it times out
        // and issues UNLINK, breaking the pipe.
        usbip_stage2_header *hdr = (usbip_stage2_header *)buf;
        hdr->base.command = htonl(USBIP_STAGE2_RSP_SUBMIT);
        hdr->base.direction = htonl(USBIP_DIR_OUT);
        memset(&(hdr->u), 0, sizeof(hdr->u));
        usbip_network_send(kSock, hdr, sizeof(usbip_stage2_header), 0);
        os_printf("fast_reply empty IN ack sent\r\n");
        return 1;
    }

    return 0;
}

void handle_dap_unlink(uint32_t seqnum)
{
    // Remove the specific IN from our queue.  Do NOT consume a response from
    // the ring-buffer: any response currently in dap_dataOUT_handle belongs to
    // a valid command and will be paired with the next waiting IN by fast_reply().
    remove_in_header_by_seqnum(seqnum);
}
