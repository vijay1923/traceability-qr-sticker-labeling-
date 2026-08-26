#ifndef COMMAND_CONSOLE_H
#define COMMAND_CONSOLE_H

#include <Arduino.h>
#include "config.h"
#include "config_manager.h"
#include "diagnostics.h"
#include "rtc_driver.h"
#include "shift_manager.h"
#include "time_sync.h"
#include "storage_manager.h"
#include "hmi.h"
// This module calls printer_manager.h functions (poll_printer_status,
// set_printer_fault, send_to_printer, printer_fault_active) via the "print"
// command - genuinely needed here, not just inherited by accident of
// #include order the way it was before this file was split from a
// single-translation-unit sketch into separate .h/.cpp pairs.
#include "printer_manager.h"
#include "web_portal.h"

// Parses and executes one line of engineer/maintenance input from Serial.
// See the "help" command (in command_console.cpp) for the full command list.
void process_serial_line(char *raw_line, uint8_t raw_len);

#endif
