#ifndef PRODUCTION_MANAGER_H
#define PRODUCTION_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "config_manager.h"
#include "diagnostics.h"
#include "state_machine.h"
#include "shift_manager.h"
#include "scanner_manager.h"
#include "qr_generator.h"
#include "printer_manager.h"
#include "rgb_status.h"

// Registered as the scanner library's callback in setup() via
// set_barcode_callback(onBarcodeScanned).
void onBarcodeScanned(const char *qrcode, int length);

#endif
