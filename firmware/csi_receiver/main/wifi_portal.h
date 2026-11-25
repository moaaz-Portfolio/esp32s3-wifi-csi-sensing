#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* Read the last successfully connected credentials from NVS. */
esp_err_t wifi_portal_load_credentials(char *ssid, size_t ssid_size,
                                       char *password, size_t password_size);

/* Prepare the temporary setup AP. Call after esp_wifi_init(). */
esp_err_t wifi_portal_prepare(void);

/* Start the HTTP setup page after esp_wifi_start() and CSI initialization. */
esp_err_t wifi_portal_start_server(void);

bool wifi_portal_is_active(void);
void wifi_portal_on_disconnected(void);
void wifi_portal_on_connected(void);
