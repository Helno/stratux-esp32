// stratux-esp32 — app entry point. Brings up NVS, the WiFi SoftAP, the GDL90
// emitter, the traffic table, and the worker tasks. See AGENTS.md for the data
// flow and task map.
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_cpu.h"

#include "settings.h"
#include "net.h"
#include "gdl90_out.h"
#include "traffic.h"
#include "modes.h"
#include "pong.h"
#include "m1421.h"
#include "gps.h"
#include "web.h"
#include "console_cmd.h"
#include "nmea_ble.h"
#include "stratux_status.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "app_main() started on core %d", esp_cpu_get_core_id());
    vTaskDelay(pdMS_TO_TICKS(100));  // Ensure logs are flushed
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    settings_load();             // NVS -> g_settings (defaults if unset)
    net_wifi_start();
    gdl90_out_init();
    traffic_init();              // table + mutex
    modes_init();                // Mode-S CRC table + ICAO address filter
    nmea_ble_init();             // BLE GATT server + NMEA formatter (M6 feature)

    // ADS-B receiver: M1421 or Pong, chosen by the m1421_en setting. They share the
    // same UART pins and are never connected together. A change needs a reboot.
    bool use_m1421 = g_settings.m1421_en;
    if (use_m1421) {
        ESP_LOGI(TAG, "Using M1421 receiver");
    } else if (g_settings.pong_en) {
        ESP_LOGI(TAG, "Using Pong radio");
    } else {
        ESP_LOGW(TAG, "No ADS-B receiver enabled");
    }

    // Worker tasks (Go goroutine -> FreeRTOS task; see AGENTS.md task map).
    if (use_m1421) {
        xTaskCreate(m1421_rx_task,    "m1421_rx",    6144, NULL, 10, NULL);
    } else if (g_settings.pong_en) {
        xTaskCreate(pong_rx_task,     "pong_rx",     4096, NULL, 10, NULL);
    }
    xTaskCreate(traffic_mgr_task, "traffic_mgr", 4096, NULL,  8, NULL);
    xTaskCreate(gdl90_emit_task,  "gdl90_emit",  4096, NULL,  9, NULL);
    if (g_settings.gps_en)
        xTaskCreate(gps_rx_task,  "gps_rx",      4096, NULL,  7, NULL);

    web_start();
    console_cmd_start();
    nmea_ble_start();            // BLE NMEA task (M6 feature)

    ESP_LOGI(TAG, "Ready: WiFi AP + GDL90 + web UI (BLE NMEA only when built with RADIO=bt)");
}
