#ifndef _UART_BRIDGE_H_
#define _UART_BRIDGE_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void uart_bridge_init();
void uart_bridge_task();
void uart_bridge_close();

// ---------------------------------------------------------------------------
// Shared UART access for the web serial terminal (see web_server.c).
//
// While a WebSocket terminal client holds the claim, uart_bridge_task stops
// draining UART RX so the two consumers do not steal bytes from each other.
// The TCP bridge connection itself stays open; it simply pauses RX forwarding.
// When USE_UART_BRIDGE is 0 these are stubs: take() always fails.
// ---------------------------------------------------------------------------

/** Claim UART RX for the WebSocket terminal. @return false if unavailable. */
bool uart_bridge_ws_take(void);

/** Release the WebSocket terminal claim. */
void uart_bridge_ws_release(void);

/**
 * @brief Read buffered UART RX bytes (non-blocking beyond @p wait_ms).
 * @return Number of bytes read, 0 when none, -1 when bridge unavailable.
 */
int uart_bridge_read(uint8_t *buf, size_t maxlen, uint32_t wait_ms);

/** Write bytes to the bridged UART TX. No-op when bridge unavailable. */
void uart_bridge_write(const uint8_t *data, size_t len);

/** Change the bridged UART baudrate. No-op when bridge unavailable. */
void uart_bridge_set_baud(int baudrate);

#endif
