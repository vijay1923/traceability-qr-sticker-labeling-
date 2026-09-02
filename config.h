#ifndef CONFIG_H
#define CONFIG_H

#define FW_VERSION      "3.1.0-noauth"
#define FW_BUILD_INFO   __DATE__ " " __TIME__

// ---- Runtime-configurable defaults (see config_manager.h) ----
// These are only the DEFAULTS used to seed NVS on first boot / after
// "resetconfig". At runtime, the actual values used everywhere in the
// firmware are g_cycle_time_s / g_inspection_window_s / g_cavity_count /
// g_shift_a_min / g_shift_b_min, loaded from NVS in setup().

#define CYCLE_TIME_S         30   // for testing
#define INSPECTION_WINDOW_S    30   // for testing
#define CAVITY_COUNT         8
// Shift start times as minutes-since-midnight so odd/non-hour boundaries
// (e.g. 07:30) are supported, not just whole hours.
#define SHIFT_A_START_MIN    (7 * 60)    // 07:00 morning 7 to evening 7
#define SHIFT_B_START_MIN    (19 * 60)   // 19:00  evening 7 to morning 7
// ------------------------------------------------------------------------------------

#define SCL_PIN 2 // i2c rtc
#define SDA_PIN 42
#define DS3231_ADDRESS 0x68

#define RGB_LED_PIN 15

#define PTR_TX 35 // serial printer
#define PTR_RX 17

#define DWIN_TX_PIN 37 // display
#define DWIN_RX_PIN 36

#define FLASH_DURATION_MS 500
#define PRINTER_POLL_INTERVAL_MS 500
#define PRINTER_FAULT_POLL_INTERVAL_MS 150  // faster polling while a fault is active, so recovery is caught sooner
#define PRINTER_LOG_HEARTBEAT_MS 5000       // repeat an unchanged fault to Serial at most this often, so it doesn't drown out the command console
#define PRINTER_REPLY_TIMEOUT_MS 500
#define PRINTER_STATUS_CMD "\x1B!?"

#define EXPECTED_DELIMITER      '*'
#define EXPECTED_SEGMENT_COUNT  4    // e.g. 1063NRF26*1190*MB*61416 -> 4 segments, 3 delimiters

// ---- Alternate scanner input format (also accepted) ----
// e.g. "V CB 84 I@290826@2979@10" -> 4 '@'-delimited segments; the batch
// number used as the traceability part number is the 3rd segment (index 2).
#define NEW_FORMAT_DELIMITER            '@'
#define NEW_FORMAT_SEGMENT_COUNT        4
#define NEW_FORMAT_BATCH_SEGMENT_INDEX  2

// ---- Cycle behavior ----
#define ENABLE_OK_NG_SUMMARY    1    // 1 = log OK/NG summary to Serial at window close, 0 = disabled
// ------------------------------------------------------------------------------------

// ---- Time sync (one-time / on-demand only, WiFi off otherwise) ----
#define TZ_OFFSET_SEC           19800  // IST = UTC+5:30 - CHANGE if different region
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define NTP_SYNC_TIMEOUT_MS     10000
// ------------------------------------------------------------------------------------

// ---- Storage: single source of truth for paths ----
#define STATS_DIR               "/stats"
#define STATS_CHECKPOINT_FILE   "/stats/current.ckp"
#define STATS_FREE_SPACE_WARN_BYTES   4096   // warn once free space below this
#define STATS_FREE_SPACE_MIN_BYTES    512    // refuse to write below this
// ------------------------------------------------------------------------------------

// ---- Serial file dump safety ----
#define MAX_GETFILE_DUMP_BYTES   65536  // cap how much a single "getfile" blocks loop() for
// ------------------------------------------------------------------------------------

#define SHIFT_CHECK_INTERVAL_MS 1000

// ---- Watchdog ----
// Worst-case normal blocking in one loop() pass today is well under 2s
// (printer status poll timeout PRINTER_REPLY_TIMEOUT_MS=500ms + a 512-byte
// flush() at 9600 baud ~500ms, plus incidentals). 8s gives a wide safety
// margin above that so it never fires during normal operation, but still
// recovers a genuinely hung board in well under a minute unattended.
#define WATCHDOG_TIMEOUT_S 8
// ------------------------------------------------------------------------------------

// ---- Web portal (WiFi AP + web dashboard) ----
// No login, no session, no rate-limiting - see web_portal.cpp and
// readme.md "Web portal" for that tradeoff. AP_SSID/AP_PASSWORD are
// hardcoded in secrets.h, not here.
#define WEBPORTAL_HTTP_PORT 80
// ------------------------------------------------------------------------------------


/// HMI Address 

#define QR_CODE 0x5000 // rm batch code - for scanned qr code and final qr code 
#define MESSAGE 0x5500  // top message line 
#define PRINT_COUNTER 0x5600  // Print counter  - ok count from perticuler cycle 
#define STICKER 0x5650  // sticker  - ok count shift wise 
#define MOLD_CAVITY 0x5700  // mold cavity  - no of mold cavities 
#define SCANNER_STATUS 0x5750  // scanner status  - connected / disconnected 
#define PRINTER_STATUS 0x5800 // printer status  - connected / disconnected 




#endif