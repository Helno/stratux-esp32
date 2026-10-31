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
#include <stdio.h>

static const char *TAG = "nmea_ble";

// SoftRF-compatible UUIDs (HM-10/UART service)
// Service: 0000ffe0-0000-1000-8000-00805f9b34fb
// Characteristic: 0000ffe1-0000-1000-8000-00805f9b34fb
static const uint8_t softrf_service_uuid[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x08, 0x80,
    0x00, 0x10, 0x00, 0x00, 0xe0, 0xff, 0x00, 0x00
};

// GATT server state
static struct {
    esp_gatt_if_t gatts_if;
    uint16_t service_handle;
    uint16_t char_handle;
    uint16_t char_cfg_handle;
    uint16_t conn_handle;
    bool connected;
    uint8_t client_count;
    bool service_ready;  // Set when service creation completes
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
    .p_service_uuid = (uint8_t *) softrf_service_uuid,
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
        
        // Configure advertisement data
        esp_ble_gap_config_adv_data(&adv_data);
        
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
        
        // Add CCCD descriptor for notifications
        esp_ble_gatts_add_char_descr(g_ble_state.service_handle,
                                      &(esp_bt_uuid_t){
                                          .len = ESP_UUID_LEN_16,
                                          .uuid.uuid16 = 0x2902  // CCCD
                                      },
                                      ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                      NULL, NULL);
        break;
        
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        // CCCD descriptor added
        g_ble_state.char_cfg_handle = param->add_char_descr.attr_handle;
        g_ble_state.service_ready = true;  // Service fully initialized!
        ESP_LOGI(TAG, "CCCD Descriptor added (handle=%d) - service ready", g_ble_state.char_cfg_handle);
        break;
        
    case ESP_GATTS_CONNECT_EVT:
        printf("\n>>> BLE CLIENT CONNECTED (conn_id=%d) <<<\n", param->connect.conn_id);
        fflush(stdout);
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
        
    case ESP_GATTS_READ_EVT:
        if (param->read.need_rsp) {
            esp_gatt_rsp_t rsp = {.attr_value = {.handle = param->read.handle, .len = 0}};
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        }
        break;
        
    case ESP_GATTS_WRITE_EVT:
        // Log all writes for debugging
        if (param->write.len <= 20) {
            char hex_buf[64] = {0};
            for (int i = 0; i < param->write.len && i < 10; i++) {
                sprintf(hex_buf + i*2, "%02x", param->write.value[i]);
            }
            ESP_LOGI(TAG, "WRITE: handle=%d len=%d data=%s", 
                    param->write.handle, param->write.len, hex_buf);
        }
        
        // Always send response if requested
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            ESP_LOGD(TAG, "Response sent for write to handle %d", param->write.handle);
        }
        break;
        
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG, "MTU exchange, MTU %d", param->mtu.mtu);
        break;
        
    default:
        break;
    }
}

// NMEA BLE task: polls traffic and sends NMEA sentences via notifications
void nmea_ble_task(void *arg) {
    printf("\n>>> NMEA BLE TASK STARTED <<<\n");
    fflush(stdout);
    char nmea_buf[256];
    traffic_info_t vehicles[TRAFFIC_TABLE_MAX];
    
    ESP_LOGI(TAG, "NMEA BLE task started");
    vTaskDelay(pdMS_TO_TICKS(1000));  // Let BLE fully settle
    
    int cycle = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));  // 0.5 Hz for testing
        cycle++;
        
        // Only send if connected
        if (g_ble_state.connected && g_ble_state.gatts_if && 
            g_ble_state.char_handle && g_ble_state.conn_handle) {
            
            const char *test_msg = "$PFLAU,0,,0,0,0,,0,0*00\r\n";
            size_t msg_len = strlen(test_msg);
            
            // First: Set the characteristic value
            esp_err_t set_err = esp_ble_gatts_set_attr_value(g_ble_state.char_handle, 
                                                              msg_len, 
                                                              (uint8_t *)test_msg);
            
            if (set_err != ESP_OK) {
                printf(">>> Set attr value failed: %s\n", esp_err_to_name(set_err));
                fflush(stdout);
                continue;
            }
            
            // Second: Send notification to all connected clients
            esp_ble_gatts_send_indicate(g_ble_state.gatts_if, g_ble_state.conn_handle,
                                       g_ble_state.char_handle,
                                       msg_len, (uint8_t *)test_msg, false);
            
            printf(">>> NMEA sent: %s", test_msg);
            fflush(stdout);
            ESP_LOGI(TAG, "Test PFLAU sent (cycle %d)", cycle);
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
    
    // Set device name
    esp_ble_gap_set_device_name("Stratux");
    
    // Register GATT app (triggers service creation in callbacks)
    esp_ble_gatts_app_register(0x55);
}

void nmea_ble_start(void) {
    printf("\n>>> nmea_ble_start() CALLED <<<\n");
    fflush(stdout);
    
    // Wait for BLE service to be fully initialized (up to 5 seconds)
    printf("Waiting for BLE service to be ready...\n");
    fflush(stdout);
    int wait_count = 0;
    while (!g_ble_state.service_ready && wait_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }
    
    if (!g_ble_state.service_ready) {
        printf(">>> BLE service initialization TIMEOUT! <<<\n");
        fflush(stdout);
        ESP_LOGE(TAG, "BLE service not ready after 5 seconds - not creating NMEA task");
        return;
    }
    printf(">>> BLE service ready! Creating NMEA task <<<\n");
    fflush(stdout);
    
    TaskHandle_t handle;
    BaseType_t result = xTaskCreate(nmea_ble_task, "nmea_ble", 4096, NULL, 5, &handle);
    if (result == pdPASS) {
        printf(">>> Task created successfully! Handle=%p <<<\n", handle);
        fflush(stdout);
        ESP_LOGI(TAG, "NMEA BLE task created OK (handle=%p)", handle);
    } else {
        printf(">>> Task creation FAILED! Result=%d <<<\n", result);
        fflush(stdout);
        ESP_LOGE(TAG, "FAILED to create NMEA BLE task! xTaskCreate returned %d", result);
    }
}

bool nmea_ble_is_connected(void) {
    return g_ble_state.connected;
}

uint8_t nmea_ble_client_count(void) {
    return g_ble_state.client_count;
}

#endif  // CONFIG_BT_BLE_ENABLED
