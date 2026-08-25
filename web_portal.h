#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>
#include "config.h"

// ---- Web dashboard over a WiFi hotspot ----
// No login, no session - anyone who connects to the hotspot (i.e. anyone
// who knows AP_SSID/AP_PASSWORD, both hardcoded in secrets.h) can see the
// dashboard immediately. This is a deliberate simplification, not an
// oversight - see readme.md "Web portal" for the tradeoff being made.

// Starts the WiFi AP (SSID/password hardcoded from secrets.h) and the web
// server. Prints SSID, password, and the portal's IP address to Serial so
// an operator knows what to connect to. No-op (with a Serial message, not
// an error) if already running.
void webportal_start();

// Stops the AP and web server.
void webportal_stop();

bool webportal_is_running();

// Call every loop() pass while the portal might be running - services any
// pending HTTP request (WebServer.h is synchronous, this is required for
// it to respond to anything at all). No-op instantly if not running.
void webportal_service();

#endif
