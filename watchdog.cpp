#include <Arduino.h>
#include <esp_task_wdt.h>
#include "watchdog.h"
#include "config.h"

void watchdog_init()
{
    esp_task_wdt_config_t twdt_config =
    {
        .timeout_ms = (uint32_t)WATCHDOG_TIMEOUT_S * 1000UL,
        .idle_core_mask = 0,     // don't watch idle tasks, only what we explicitly add below
        .trigger_panic = true,   // reset the chip on timeout rather than just interrupting
    };

    esp_err_t err = esp_task_wdt_init(&twdt_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) // INVALID_STATE = already initialized, harmless
    {
        Serial.print("WARN - watchdog init failed, err=");
        Serial.println((int)err);
        return;
    }

    esp_task_wdt_add(NULL); // subscribe the current task (Arduino's loopTask)
    Serial.print("Watchdog armed, timeout=");
    Serial.print(WATCHDOG_TIMEOUT_S);
    Serial.println("s");
}

void watchdog_feed()
{
    esp_task_wdt_reset();
}
