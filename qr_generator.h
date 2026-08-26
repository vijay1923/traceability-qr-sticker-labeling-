#ifndef QR_GENERATOR_H
#define QR_GENERATOR_H

#include <Arduino.h>
#include "config.h"
#include "rtc_driver.h"
#include "shift_manager.h"

extern char final_qr[30]; // printing qr code data - defined in qr_generator.cpp

bool generate_traceability_qr(const char *part_number);
bool validate_final_qr(const char *qr);

#endif
