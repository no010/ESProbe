/**
 * @file tcp_server.c
 * @brief Handle main tcp tasks
 * @version 0.1
 * @date 2020-01-22
 *
 * @copyright Copyright (c) 2020
 *
 */
#include <string.h>
#include <stdint.h>
#include <sys/param.h>

#include "main/wifi_configuration.h"
#include "main/usbip_server.h"
#include "main/DAP_handle.h"
#include "main/access_control.h"

#include "components/elaphureLink/elaphureLink_protocol.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

extern TaskHandle_t kDAPTaskHandle;
extern int kRestartDAPHandle;

int kSock = -1;

// Extract the client IPv4 address (network byte order) from a connected
// socket. Handles both plain AF_INET and IPv4-mapped IPv6 peers (the
// listener is dual-stack). Returns 0 when the address cannot be resolved.
static uint32_t client_ip4_of_sock(int sock)
{
    struct sockaddr_storage peer;
    socklen_t len = sizeof(peer);
    if (getpeername(sock, (struct sockaddr *)&peer, &len) != 0)
        return 0;

    if (peer.ss_family == AF_INET) {
        return ((struct sockaddr_in *)&peer)->sin_addr.s_addr;
    }
#if LWIP_IPV6
    if (peer.ss_family == AF_INET6) {
        const struct sockaddr_in6 *p6 = (const struct sockaddr_in6 *)&peer;
        const uint8_t *b = (const uint8_t *)p6->sin6_addr.un.u8_addr;
        // IPv4-mapped: ::ffff:a.b.c.d
        static const uint8_t v4_mapped_prefix[12] =
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        if (memcmp(b, v4_mapped_prefix, sizeof(v4_mapped_prefix)) == 0) {
            uint32_t ip;
            memcpy(&ip, b + 12, 4);
            return ip;
        }
    }
#endif
    return 0;
}

void tcp_server_task(void *pvParameters)
{
    uint8_t tcp_rx_buffer[1500] = {0};
    char addr_str[128];
    enum usbip_server_state_t usbip_state = WAIT_DEVLIST;
    uint8_t *data;
    int addr_family;
    int ip_protocol;
    int header;
    int ret, sz;

    int on = 1;
    while (1)
    {

#ifdef CONFIG_EXAMPLE_IPV4
        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
#else // IPV6
        struct sockaddr_in6 destAddr;
        bzero(&destAddr.sin6_addr.un, sizeof(destAddr.sin6_addr.un));
        destAddr.sin6_family = AF_INET6;
        destAddr.sin6_port = htons(PORT);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
        inet6_ntoa_r(destAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif

        int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (listen_sock < 0)
        {
            os_printf("Unable to create socket: errno %d\r\n", errno);
            break;
        }
        os_printf("Socket created\r\n");

        setsockopt(listen_sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
        setsockopt(listen_sock, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on));

        int err = bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (err != 0)
        {
            os_printf("Socket unable to bind: errno %d\r\n", errno);
            break;
        }
        os_printf("Socket binded\r\n");

        err = listen(listen_sock, 1);
        if (err != 0)
        {
            os_printf("Error occured during listen: errno %d\r\n", errno);
            break;
        }
        os_printf("Socket listening\r\n");

#ifdef CONFIG_EXAMPLE_IPV6
        struct sockaddr_in6 sourceAddr; // Large enough for both IPv4 or IPv6
#else
        struct sockaddr_in sourceAddr;
#endif
        uint32_t addrLen = sizeof(sourceAddr);
        while (1)
        {
            kSock = accept(listen_sock, (struct sockaddr *)&sourceAddr, &addrLen);
            if (kSock < 0)
            {
                os_printf("Unable to accept connection: errno %d\r\n", errno);
                break;
            }
            setsockopt(kSock, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
            setsockopt(kSock, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on));
            os_printf("Socket accepted\r\n");

            // Optional PIN gate: reject clients not unlocked via web portal.
            if (!access_control_is_allowed(client_ip4_of_sock(kSock)))
            {
                os_printf("Client rejected by access control (unlock via web portal)\r\n");
                close(kSock);
                kSock = -1;
                continue;
            }

            // Read header
            sz = 4;
            data = &tcp_rx_buffer[0];
            do {
                ret = recv(kSock, data, sz, 0);
                if (ret <= 0)
                    goto cleanup;
                sz -= ret;
                data += ret;
            } while (sz > 0);

            header = *((int *)(tcp_rx_buffer));
            header = ntohl(header);
            os_printf("tcp_server header=0x%08X low=0x%04X buf=%02X%02X%02X%02X\r\n",
                      (unsigned)header, (unsigned)(header & 0xFFFF),
                      tcp_rx_buffer[0], tcp_rx_buffer[1], tcp_rx_buffer[2], tcp_rx_buffer[3]);

            if (header == EL_LINK_IDENTIFIER) {
                el_dap_work(tcp_rx_buffer, sizeof(tcp_rx_buffer));
            } else if ((header & 0xFFFF) == 0x8003 ||
                       (header & 0xFFFF) == 0x8005) { // usbip OP_REQ_DEVLIST/OP_REQ_IMPORT
                if ((header & 0xFFFF) == 0x8005)
                    usbip_state = WAIT_DEVLIST;
                else
                    usbip_state = WAIT_IMPORT;
                usbip_worker(tcp_rx_buffer, sizeof(tcp_rx_buffer), &usbip_state);
            } else {
                os_printf("Unknown protocol\n");
            }

cleanup:
            if (kSock != -1)
            {
                os_printf("Shutting down socket and restarting...\r\n");
                //shutdown(kSock, 0);
                close(kSock);

                el_process_buffer_free();

                // Restart DAP Handle
                kRestartDAPHandle = RESET_HANDLE;
                if (kDAPTaskHandle)
                    xTaskNotifyGive(kDAPTaskHandle);

                //shutdown(listen_sock, 0);
                //close(listen_sock);
                //vTaskDelay(5);
            }
        }
    }
    vTaskDelete(NULL);
}