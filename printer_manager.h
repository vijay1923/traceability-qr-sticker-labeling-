#ifndef PRINTER_MANAGER_H
#define PRINTER_MANAGER_H

#include <HardwareSerial.h>
#include "config.h"
#include "hmi.h"
#include "state_machine.h"

HardwareSerial &printer_port = Serial2;

bool printer_tx_busy = false;
unsigned long last_printer_poll_ms = 0;
bool last_printer_response_ok = false;
bool last_printer_status_valid = false;
unsigned long last_printer_serial_log_ms = 0;

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

printer_status_t last_logged_printer_status = PRINTER_STATUS_OTHER_ERROR;

// Set by set_printer_fault() (below). When true, the printer is either not
// responding or reporting a non-NORMAL status - onBarcodeScanned() checks
// this FIRST and rejects the scan immediately, before doing any other work,
// so a known-bad printer blocks the whole cycle, not just the print step.
bool printer_fault_active = false;
printer_status_t printer_fault_status = PRINTER_STATUS_NORMAL; // meaningless while printer_fault_active == false

const char *printer_status_to_text(printer_status_t status)
{
    switch (status)
    {
        case PRINTER_STATUS_NORMAL: return "NORMAL";
        case PRINTER_STATUS_HEAD_OPEN: return "HEAD_OPEN";
        case PRINTER_STATUS_PAPER_JAM: return "PAPER_JAM";
        case PRINTER_STATUS_PAPER_JAM_HEAD_OPEN: return "PAPER_JAM_HEAD_OPEN";
        case PRINTER_STATUS_OUT_OF_PAPER: return "OUT_OF_PAPER";
        case PRINTER_STATUS_OUT_OF_PAPER_HEAD_OPEN: return "OUT_OF_PAPER_HEAD_OPEN";
        case PRINTER_STATUS_OUT_OF_RIBBON: return "OUT_OF_RIBBON";
        case PRINTER_STATUS_OUT_OF_RIBBON_HEAD_OPEN: return "OUT_OF_RIBBON_HEAD_OPEN";
        case PRINTER_STATUS_OUT_OF_RIBBON_PAPER_JAM: return "OUT_OF_RIBBON_PAPER_JAM";
        case PRINTER_STATUS_OUT_OF_RIBBON_PAPER_JAM_HEAD_OPEN: return "OUT_OF_RIBBON_PAPER_JAM_HEAD_OPEN";
        case PRINTER_STATUS_OUT_OF_RIBBON_OUT_OF_PAPER: return "OUT_OF_RIBBON_OUT_OF_PAPER";
        case PRINTER_STATUS_OUT_OF_RIBBON_OUT_OF_PAPER_HEAD_OPEN: return "OUT_OF_RIBBON_OUT_OF_PAPER_HEAD_OPEN";
        case PRINTER_STATUS_PAUSED: return "PAUSED";
        case PRINTER_STATUS_PRINTING: return "PRINTING";
        case PRINTER_STATUS_OTHER_ERROR: return "OTHER_ERROR";
        default: return "UNKNOWN_COMPOSITE_STATUS";
    }
}

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
void set_printer_fault(bool active, printer_status_t status, bool responded = true)
{
    printer_fault_active = active;
    printer_fault_status = active ? status : PRINTER_STATUS_NORMAL;

    // PRINTER_STATUS: always just Connected/Disconnected. A printer that
    // responded with a bad status (paper jam etc.) is still physically
    // connected - it answered us - so that's "Connected", not a fault code.
    hmi_write_text((uint16_t)PRINTER_STATUS, String(responded ? "Connected" : "Disconnected"));

    if (active)
    {
        const char *status_text = responded ? printer_status_to_text(status) : "Disconnected";
        hmi_condition_set(HMI_COND_PRINTER, String("Printer Fault: ") + status_text, HMI_SEV_PRINTER);
    }
    else
    {
        hmi_condition_clear(HMI_COND_PRINTER);
    }

    window_paused_for_printer = active;
    window_pause_recompute();
}

void printer_init()
{
    printer_port.begin(9600, SERIAL_8N1, PTR_RX, PTR_TX);  // printer initialization
    last_printer_poll_ms = millis();
    last_printer_response_ok = false;
    last_printer_status_valid = false;
    Serial.println("Printer Initialized");
}

bool build_tspl_qr_job(const char *qr_data, char *out_buf, size_t out_buf_size, size_t &out_len)
{
    if (qr_data == nullptr || qr_data[0] == '\0' || out_buf == nullptr || out_buf_size == 0)
    {
        return false;
    }

    int n = snprintf
    (
        out_buf,
        out_buf_size,
        "SIZE 16 mm,16 mm\r\n"
        "GAP 3 mm,3 mm\r\n"
        "DIRECTION 1\r\n"
        "CLS\r\n"
        "DMATRIX 15,0,128,128,x5,\"%s\"\r\n"
        "PRINT 1,1\r\n",
        qr_data);

    if (n <= 0 || (size_t)n >= out_buf_size)
    {
        return false;
    }

    out_len = (size_t)n;
    return true;
}

bool poll_printer_status(printer_status_t &status)
{
    while (printer_port.available() > 0)
    {
        (void)printer_port.read();
    }

    printer_port.write(PRINTER_STATUS_CMD, sizeof(PRINTER_STATUS_CMD));

    unsigned long start_ms = millis();
    while ((millis() - start_ms) < PRINTER_REPLY_TIMEOUT_MS)
    {
        if (printer_port.available() > 0)
        {
            status = (printer_status_t)printer_port.read();
            return true;
        }
    }

    return false;
}

void send_to_printer(const char *label)
{
    Serial.print("PRINT -> ");
    Serial.println(label);

    char job_buf[512];
    size_t job_len = 0;

    if (!build_tspl_qr_job(label, job_buf, sizeof(job_buf), job_len))
    {
        Serial.println("PRINT FAILED: TSPL build error");
        hmi_condition_set(HMI_COND_PRINTER, String("Print Build Error"), HMI_SEV_PRINTER);
        return;
    }

    printer_tx_busy = true;
    size_t bytes_sent = printer_port.write((const uint8_t *)job_buf, job_len);
    printer_port.flush();
    printer_tx_busy = false;

    Serial.print("PRINT BYTES -> ");
    Serial.print((unsigned)bytes_sent);
    Serial.print("/");
    Serial.println((unsigned)job_len);
}

#endif