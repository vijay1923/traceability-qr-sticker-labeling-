#ifndef CONFIG_H
#define CONFIG_H

#define FW_VERSION      "1.1.0"
#define FW_BUILD_INFO   __DATE__ " " __TIME__

// ---- Runtime-configurable defaults (see config_manager.h) ----
// These are only the DEFAULTS used to seed NVS on first boot / after
// "resetconfig". At runtime, the actual values used everywhere in the
// firmware are g_cycle_time_s / g_inspection_window_s / g_cavity_count /
// g_shift_a_hour / g_shift_b_hour, loaded from NVS in setup().

#define CYCLE_TIME_S         30   // for testing
#define SPECTION_WINDOW_S    30   // for testing
#define CAVITY_COUNT         8
#define SHIFT_A_START_HOUR   7    // 07:00 morning 7 to evening 7 
#define SHIFT_B_START_HOUR   19   // 19:00  evening 7 to morning 7
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


/// HMI Address 

#define QR_CODE 0x5000 // rm batch code - for scanned qr code and final qr code 
#define MESSAGE 0x5500  // top message line 
#define PRINT_COUNTER 0x5600  // Print counter  - ok count from perticuler cycle 
#define STICKER 0x5650  // sticker  - ok count shift wise 
#define MOLD_CAVITY 0x5700  // mold cavity  - no of mold cavities 
#define SCANNER_STATUS 0x5750  // scanner status  - connected / disconnected 
#define PRINTER_STATUS 0x5800 // printer status  - connected / disconnected 




#endif