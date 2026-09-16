#include "sdkconfig.h"

#ifdef CONFIG_BT_BLE_ENABLED

#include "nmea_ble.h"
#include "nmea_formatter.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatt_defs.h"
#include "traffic.h"
#include "settings.h"

static const char *TAG = "nmea_ble";

// SoftRF-compatible UUIDs (HM-10/UART service)
#define SOFTRF_SERVICE_UUID     "0000ffe0-0000-1000-8000-00805f9b34fb"
#define SOFTRF_CHAR_UUID        "0000ffe1-0000-1000-8000-00805f9b34fb"

// GATT server state
static struct {
    esp_gatt_if_t gatts_if;
    uint16_t service_handle;
    uint16_t char_handle;
    uint16_t char_cfg_handle;
    uint16_t conn_handle;
    bool connected;
    uint8_t client_count;
} g_ble_state = {0};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,  // min conn interval 7.5ms
    .max_interval = 0x0010,  // max conn interval 20ms
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 16,
    .p_service_uuid = (uint8_t *) SOFTRF_SERVICE_UUID,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x0020,
    .adv_int_max = 0x0040,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// GAP event handler
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "BLE advertising started");
        } else {
            ESP_LOGE(TAG, "BLE advertising failed");
        }
        break;
    default:
        break;
    }
}

// GATT event handler
static void gatt_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                               esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        g_ble_state.gatts_if = gatts_if;
        ESP_LOGI(TAG, "GATT app registered (if=%d)", gatts_if);
        
        // Create service
        esp_ble_gatts_create_service(gatts_if, &(esp_gatt_srvc_id_t){
            .is_primary = true,
            .id.inst_id = 0,
            .id.uuid.len = ESP_UUID_LEN_128,
            .id.uuid.uuid.uuid128 = {
                0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x08, 0x80,
                0x00, 0x10, 0x00, 0x00, 0xe0, 0xff, 0x00, 0x00
            }
        }, 4);
        break;
        
    case ESP_GATTS_CREATE_EVT:
        g_ble_state.service_handle = param->create.service_handle;
        ESP_LOGI(TAG, "Service created (handle=%d)", g_ble_state.service_handle);
        
        // Start service
        esp_ble_gatts_start_service(g_ble_state.service_handle);
        
        // Add characteristic
        esp_ble_gatts_add_char(g_ble_state.service_handle,
                               &(esp_bt_uuid_t){
                                   .len = ESP_UUID_LEN_128,
                                   .uuid.uuid128 = {
                                       0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x08, 0x80,
                                       0x00, 0x10, 0x00, 0x00, 0xe1, 0xff, 0x00, 0x00
                                   }
                               },
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_READ |
                               ESP_GATT_CHAR_PROP_BIT_NOTIFY |
                               ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                               NULL, NULL);
        break;
        
    case ESP_GATTS_ADD_CHAR_EVT:
        g_ble_state.char_handle = param->add_char.attr_handle;
        ESP_LOGI(TAG, "Characteristic added (handle=%d)", g_ble_state.char_handle);
        
        // Add descriptor for notifications
        esp_ble_gatts_add_char_descr(g_ble_state.service_handle,
                                      &(esp_bt_uuid_t){
                                          .len = ESP_UUID_LEN_16,
                                          .uuid.uuid16 = 0x2902  // CCCD
                                      },
                                      ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                      NULL, NULL);
        break;
        
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        g_ble_state.char_cfg_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(TAG, "Descriptor added (handle=%d)", g_ble_state.char_cfg_handle);
        break;
        
    case ESP_GATTS_CONNECT_EVT:
        g_ble_state.conn_handle = param->connect.conn_id;
        g_ble_state.connected = true;
        g_ble_state.client_count++;
        ESP_LOGI(TAG, "BLE client connected (conn_id=%d, total=%d)",
                 param->connect.conn_id, g_ble_state.client_count);
        break;
        
    case ESP_GATTS_DISCONNECT_EVT:
        g_ble_state.connected = false;
        if (g_ble_state.client_count > 0) {
            g_ble_state.client_count--;
        }
        ESP_LOGI(TAG, "BLE client disconnected (total=%d)", g_ble_state.client_count);
        esp_ble_gap_start_advertising(&adv_params);
        break;
        
    default:
        break;
    }
}

// NMEA BLE task: polls traffic and sends NMEA sentences via notifications
void nmea_ble_task(void *arg) {
    char nmea_buf[256];
    traffic_info_t vehicles[TRAFFIC_TABLE_MAX];
    
    ESP_LOGI(TAG, "NMEA BLE task started");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // 1 Hz update rate
        
        // Only send if connected
        if (!g_ble_state.connected || g_ble_state.gatts_if == 0) {
            continue;
        }
        
        // Get traffic snapshot
        size_t count = traffic_snapshot(vehicles, TRAFFIC_TABLE_MAX);
        
        // Send PFLAU (own-ship status) first
        size_t highest_priority = 0;
        if (count > 0) {
            // Find highest-priority target (for now, just first valid one)
            for (size_t i = 0; i < count; i++) {
                if (vehicles[i].position_valid) {
                    highest_priority = i;
                    break;
                }
            }
        }
        
        size_t pflau_len = nmea_format_pflau(count,
                                              count > 0 ? &vehicles[highest_priority] : NULL,
                                              false,  // gps_valid (M3+)
                                              nmea_buf, sizeof(nmea_buf));
        
        if (pflau_len > 0) {
            esp_ble_gatts_send_indicate(g_ble_state.gatts_if, g_ble_state.conn_handle,
                                         g_ble_state.char_handle,
                                         pflau_len, (uint8_t *)nmea_buf, false);
            ESP_LOGD(TAG, "PFLAU: %s", nmea_buf);
        }
        
        // Send PFLAA for each traffic entry
        for (size_t i = 0; i < count; i++) {
            if (!vehicles[i].position_valid) {
                continue;
            }
            
            size_t pflaa_len = nmea_format_pflaa(&vehicles[i],
                                                  0.0, 0.0,  // Placeholder ownship (M3+)
                                                  nmea_buf, sizeof(nmea_buf));
            
            if (pflaa_len > 0) {
                esp_ble_gatts_send_indicate(g_ble_state.gatts_if, g_ble_state.conn_handle,
                                             g_ble_state.char_handle,
                                             pflaa_len, (uint8_t *)nmea_buf, false);
                ESP_LOGD(TAG, "PFLAA[%zu]: %s", i, nmea_buf);
            }
            
            // Yield to other tasks
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void nmea_ble_init(void) {
    ESP_LOGI(TAG, "Initializing BLE NMEA");
    
    // Enable BT
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    
    // Init Bluedroid
    esp_bluedroid_init();
    esp_bluedroid_enable();
    
    // Register GAP and GATT callbacks
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_register_callback(gatt_event_handler);
    
    // Register GATT app
    esp_ble_gatts_app_register(0x55);
}

void nmea_ble_start(void) {
    ESP_LOGI(TAG, "Starting NMEA BLE");
    xTaskCreate(nmea_ble_task, "nmea_ble", 4096, NULL, 5, NULL);
}

bool nmea_ble_is_connected(void) {
    return g_ble_state.connected;
}

uint8_t nmea_ble_client_count(void) {
    return g_ble_state.client_count;
}

#endif  // CONFIG_BT_BLE_ENABLED
