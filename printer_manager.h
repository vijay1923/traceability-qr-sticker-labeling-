#ifndef PRINTER_MANAGER_H
#define PRINTER_MANAGER_H

#include <HardwareSerial.h>
#include "config.h"
#include "hmi.h"

HardwareSerial &printer_port = Serial2;

bool printer_tx_busy = false;
unsigned long last_printer_poll_ms = 0;
bool last_printer_response_ok = false;
bool last_printer_status_valid = false;

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

// ---- Printer fault cache (backs Gate #0 - fast reject without a live poll) ----
// Gate #2 (pre-print) always does its own fresh poll regardless of this cache,
// so this never weakens the actual print-time safety check - it only lets
// onBarcodeScanned() fail fast on scans it already knows will be rejected,
// instead of paying a poll wait for every single scan while the printer is
// known to be down.
bool printer_fault_active = false;
printer_status_t printer_fault_status = PRINTER_STATUS_NORMAL;

// Single place that updates the fault cache AND drives the HMI fault
// message, so every poll site (boot, periodic, pre-print, post-print) gets
// full treatment just by calling this instead of duplicating logic.
void set_printer_fault_state(bool poll_ok, printer_status_t status)
{
    bool was_fault = printer_fault_active;

    printer_fault_active = !(poll_ok && status == PRINTER_STATUS_NORMAL);
    printer_fault_status = poll_ok ? status : PRINTER_STATUS_OTHER_ERROR;

    if (printer_fault_active)
    {
        char msg[40];
        snprintf(msg, sizeof(msg), "Printer Fault: %s",
                 poll_ok ? printer_status_to_text(status) : "NO RESPONSE");
        hmi_show_message(msg, HMI_MSG_FAULT);
    }
    else if (was_fault)
    {
        // Fault just cleared - release the priority hold so routine state
        // messages can display again, then show a routine confirmation.
        hmi_message_priority = HMI_MSG_ROUTINE;
        hmi_show_message("Printer OK", HMI_MSG_ROUTINE);
    }
}
// --------------------------------------------------------------------------

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

    int n = snprintf(
        out_buf,
        out_buf_size,
        "SIZE 60 mm,40 mm\r\n"
        "GAP 0 mm,0 mm\r\n"
        "DIRECTION 1\r\n"
        "CLS\r\n"
        "QRCODE 100,100,H,4,A,0,\"%s\"\r\n"
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
        hmi.Write_UString((uint16_t)MESSAGE, String("Print Build Error"));
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