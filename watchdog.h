#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <Arduino.h>

// ---- Hardware task watchdog ----
// If loop() ever fails to return within WATCHDOG_TIMEOUT_S (e.g. stuck in a
// blocking UART wait due to a hardware fault, or any other unforeseen hang),
// the ESP-IDF task watchdog panics and resets the chip instead of leaving an
// unattended floor controller frozen indefinitely. Timeout is set well above
// the worst normal-case blocking we have today (printer status poll timeout
// + printer_port.flush() at 9600 baud is well under 2s) so it only fires on
// a genuine hang, not on normal operation.
//
// watchdog_feed() must be called every loop() iteration - see main.ino. If
// you add a new blocking wait anywhere that could plausibly run longer than
// WATCHDOG_TIMEOUT_S, either raise the timeout or make that wait
// non-blocking; don't just feed the watchdog from inside the wait, that
// defeats its purpose.
void watchdog_init();
void watchdog_feed();

#endif
