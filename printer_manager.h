#ifndef PRINTER_MANAGER_H
#define PRINTER_MANAGER_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include "config.h"
#include "hmi.h"
#include "state_machine.h"

// Defined in printer_manager.cpp.
extern bool printer_tx_busy;
extern unsigned long last_printer_poll_ms;
extern bool last_printer_response_ok;
extern bool last_printer_status_valid;
extern unsigned long last_printer_serial_log_ms;

typedef enum : uint8_t
{
    PRINTER_STATUS_NORMAL                                = 0x00,

    PRINTER_STATUS_HEAD_OPEN                             = 0x01,

    PRINTER_STATUS_PAPER_JAM                             = 0x02,
    PRINTER_STATUS_PAPER_JAM_HEAD_OPEN                   = 0x03,

    PRINTER_STATUS_OUT_OF_PAPER                          = 0x04,
    PRINTER_STATUS_OUT_OF_PAPER_HEAD_OPEN                = 0x05,

    PRINTER_STATUS_OUT_OF_RIBBON                         = 0x08,
    PRINTER_STATUS_OUT_OF_RIBBON_HEAD_OPEN               = 0x09,
    PRINTER_STATUS_OUT_OF_RIBBON_PAPER_JAM               = 0x0A,
    PRINTER_STATUS_OUT_OF_RIBBON_PAPER_JAM_HEAD_OPEN     = 0x0B,
    PRINTER_STATUS_OUT_OF_RIBBON_OUT_OF_PAPER            = 0x0C,
    PRINTER_STATUS_OUT_OF_RIBBON_OUT_OF_PAPER_HEAD_OPEN  = 0x0D,

    PRINTER_STATUS_PAUSED                                = 0x10,
    PRINTER_STATUS_PRINTING                              = 0x20,

    PRINTER_STATUS_OTHER_ERROR                           = 0x80

} printer_status_t; // Printer Status Codes (from TSPL command set)

extern printer_status_t last_logged_printer_status;

// Set by set_printer_fault() (below). When true, the printer is either not
// responding or reporting a non-NORMAL status - onBarcodeScanned() checks
// this FIRST and rejects the scan immediately, before doing any other work,
// so a known-bad printer blocks the whole cycle, not just the print step.
extern bool printer_fault_active;
extern printer_status_t printer_fault_status; // meaningless while printer_fault_active == false

const char *printer_status_to_text(printer_status_t status);

// Central place to raise or clear a printer fault. Every call site that
// discovers a printer problem (the periodic background poll, the pre-print
// check, the post-print check, or the boot-time check) should call this
// instead of touching printer_fault_active/HMI directly - it keeps all of
// them in sync and, as a side effect, pauses/resumes the INSPECTION_WINDOW
// countdown (see state_machine.h) so a printer outage doesn't get counted
// as NG cavities.
//   active    - true if the printer currently has a problem
//   status    - the status code, when the printer did respond
//   responded - false if the printer gave no reply at all (vs. replying
//               with a bad status) - PRINTER_STATUS only ever shows
//               Connected/Disconnected based on this; the actual fault
//               detail (jam, out of paper, etc.) goes to MESSAGE only.
void set_printer_fault(bool active, printer_status_t status, bool responded = true);

void printer_init();

// ---- Blocking status poll ----
// Used for the boot check, the manual "printer status" console command, and
// the pre/post-print checks in production_manager.h - all one-off, user- or
// scan-triggered calls where a short block is expected and acceptable. See
// bg_poll_printer_status_start()/_service() below for the non-blocking
// version used by the periodic background poll in loop().
bool poll_printer_status(printer_status_t &status);

// ---- Non-blocking background poll ----
// Used only by the periodic health poll in loop() (see main.ino). The
// background poll runs every single loop() pass forever, so it's the one
// that actually needed to stop spending up to PRINTER_REPLY_TIMEOUT_MS
// blocking the whole system on every cycle.
typedef enum : uint8_t
{
    BG_POLL_IDLE = 0,
    BG_POLL_WAITING_REPLY,
} bg_poll_state_t;

extern bg_poll_state_t bg_poll_state;

// Call once to kick off a background poll: flushes stale RX bytes and sends
// the status request, then returns immediately without waiting for a reply.
// Caller (main.ino) must only call this when bg_poll_state == BG_POLL_IDLE.
void bg_poll_printer_status_start();

// Call every loop() pass. Returns true exactly once a result is ready
// (reply received, or PRINTER_REPLY_TIMEOUT_MS elapsed with no reply) and
// writes it to status/ok, leaving bg_poll_state back at BG_POLL_IDLE.
// Returns false while still waiting, or if no poll is in flight - caller
// should do nothing in that case.
bool bg_poll_printer_status_service(printer_status_t &status, bool &ok);

void send_to_printer(const char *label);

#endif
