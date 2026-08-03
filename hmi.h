#ifndef HMI_H
#define HMI_H

/// Dwin HMI dsplay address
#define OK_COUNT_ADDR 0x5000
#define CYCLE_COUNT_ADDR 0x5001
#define QR_CODE_ADDR 0x5002
#define GET_DETAILS_ADDR 0x5003

void hmi_setup()
{
    Serial2.begin(9600, SERIAL_8N1, DWIN_RX_PIN, DWIN_TX_PIN);
    Serial.println("HMI Initialized");
}



#endif