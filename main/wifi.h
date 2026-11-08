#pragma once

#include <stdbool.h>

// Connects to the network named in config.json and keeps reconnecting.
void wifi_start(const char *ssid, const char *password);

// Waits up to timeout_ms for an IP address.
bool wifi_wait(int timeout_ms);
