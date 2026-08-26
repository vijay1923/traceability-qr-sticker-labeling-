#include <Arduino.h>
#include "printer_manager.h"

static HardwareSerial &printer_port = Serial2;

bool printer_tx_busy = false;
unsigned long last_printer_poll_ms = 0;
bool last_printer_response_ok = false;
bool last_printer_status_valid = false;
unsigned long last_printer_serial_log_ms = 0;

printer_status_t last_logged_printer_status = PRINTER_STATUS_OTHER_ERROR;

bool printer_fault_active = false;
printer_status_t printer_fault_status = PRINTER_STATUS_NORMAL;

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

void set_printer_fault(bool active, printer_status_t status, bool responded)
{
    printer_fault_active = active;
    printer_fault_status = active ? status : PRINTER_STATUS_NORMAL;

    // PRINTER_STATUS: always just Connected/Disconnected. A printer that
    // responded with a bad status (paper jam etc.) is still physically
    // connected - it answered us - so that's "Connected", not a fault code.
    hmi.Write_UString((uint16_t)PRINTER_STATUS, String(responded ? "Connected" : "Disconnected"));

    if (active)
    {
        const char *status_text = responded ? printer_status_to_text(status) : "Disconnected";
        // snprintf into a stack buffer, not String concatenation: this runs
        // every poll cycle (every 150-500ms) for as long as the fault
        // persists, so it must not touch the heap at all.
        char fault_msg[HMI_CONDITION_TEXT_LEN];
        snprintf(fault_msg, sizeof(fault_msg), "Printer Fault: %s", status_text);
        hmi_condition_set(HMI_COND_PRINTER, fault_msg, HMI_SEV_PRINTER);
    }
    else
    {
        hmi_condition_clear(HMI_COND_PRINTER);
    }

    window_paused_for_printer = active;
    window_pause_recompute();
    system_fault_recompute();
}

void printer_init()
{
    printer_port.begin(9600, SERIAL_8N1, PTR_RX, PTR_TX);  // printer initialization
    last_printer_poll_ms = millis();
    last_printer_response_ok = false;
    last_printer_status_valid = false;
    Serial.println("Printer Initialized");
}

static bool build_tspl_qr_job(const char *qr_data, char *out_buf, size_t out_buf_size, size_t &out_len)
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

bg_poll_state_t bg_poll_state = BG_POLL_IDLE;
static unsigned long bg_poll_request_ms = 0;

bool poll_printer_status(printer_status_t &status)
{
    // Take the link back from the background poll: whatever it sent is
    // about to be superseded by our own request below, and any reply that
    // shows up from here on belongs to US, not it. Without this, a
    // background poll left WAITING_REPLY when a scan calls this (Gate #2/#3
    // in production_manager.h) could have its late reply misread here, or
    // this call's flush could eat the reply the background poll was
    // waiting on - either way, stale/misattributed status data. Resetting
    // to IDLE just means the background poll re-asks at its next scheduled
    // interval, which is harmless.
    bg_poll_state = BG_POLL_IDLE;

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

void bg_poll_printer_status_start()
{
    while (printer_port.available() > 0)
    {
        (void)printer_port.read();
    }

    printer_port.write(PRINTER_STATUS_CMD, sizeof(PRINTER_STATUS_CMD));
    bg_poll_request_ms = millis();
    bg_poll_state = BG_POLL_WAITING_REPLY;
}

bool bg_poll_printer_status_service(printer_status_t &status, bool &ok)
{
    if (bg_poll_state != BG_POLL_WAITING_REPLY)
    {
        return false;
    }

    if (printer_port.available() > 0)
    {
        status = (printer_status_t)printer_port.read();
        ok = true;
        bg_poll_state = BG_POLL_IDLE;
        return true;
    }

    if ((millis() - bg_poll_request_ms) >= PRINTER_REPLY_TIMEOUT_MS)
    {
        ok = false;
        bg_poll_state = BG_POLL_IDLE;
        return true;
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
        hmi_condition_set(HMI_COND_PRINTER, "Print Build Error", HMI_SEV_PRINTER);
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
