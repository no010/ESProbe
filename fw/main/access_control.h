/**
 * @file access_control.h
 * @brief Optional PIN-based access control for debug ports (TCP 3240/1234).
 *
 * Design:
 *  - A PIN is stored in NVS (namespace "authcfg"). When no PIN is set the
 *    device behaves exactly as before: all clients are allowed (backward
 *    compatible with existing OpenOCD/Keil/elaphureLink setups).
 *  - When a PIN is set, TCP debug/UART-bridge connections are only accepted
 *    from client IPs that have been unlocked via the web portal
 *    (POST /unlock with the correct PIN). Unlocks last until reboot.
 *  - The web portal itself (port 80) is never gated, so the device can
 *    always be unlocked or reconfigured.
 *
 * Note: the PIN travels over plain HTTP on the local network. This is a
 * tamper deterrent for trusted LANs, not cryptographic security.
 */
#ifndef _ACCESS_CONTROL_H_
#define _ACCESS_CONTROL_H_

#include <stdint.h>
#include <stdbool.h>

#define ACCESS_PIN_MAX_LEN   16  // chars + NUL fits NVS str comfortably
#define ACCESS_MAX_SESSIONS  4   // simultaneously unlocked client IPs

/** Load PIN state from NVS. Call once at boot before servers start. */
void access_control_init(void);

/** @return true if a PIN is configured (gating active). */
bool access_control_has_pin(void);

/**
 * @brief Check whether a client IPv4 address may use the debug/UART ports.
 * @param ip Client address in network byte order (sin_addr.s_addr).
 * @return true when no PIN is set, or the IP has been unlocked.
 */
bool access_control_is_allowed(uint32_t ip);

/**
 * @brief Validate PIN and unlock the client IP until reboot.
 * @return 0 on success, -1 on wrong PIN, -2 when session table is full.
 */
int access_control_unlock(uint32_t ip, const char *pin);

/**
 * @brief Set or clear the PIN (empty string clears) and persist to NVS.
 *
 * When a PIN is already configured, the caller must pass the current PIN
 * in @p old_pin (checked before applying). When none is set, @p old_pin
 * is ignored.
 *
 * @return 0 on success, -1 on wrong old PIN, -2 on NVS failure.
 */
int access_control_set_pin(const char *new_pin, const char *old_pin);

#endif /* _ACCESS_CONTROL_H_ */
