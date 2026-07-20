#ifndef _WIFI_HANDLE_H_
#define _WIFI_HANDLE_H_

#include <stdbool.h>

void wifi_init();

// Returns true if the device fell back to SoftAP mode (STA connection failed).
bool wifi_is_ap_mode(void);

#endif
