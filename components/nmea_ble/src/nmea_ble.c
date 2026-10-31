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

// Nordic UART Service (NUS) - used by MB205 and SkyTraffic
// Service: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
// TX (notify): 6e400003-b5a3-f393-e0a9-e50e24dcca9e
// RX (write):  6e400002-b5a3-f393-e0a9-e50e24dcca9e

static const uint8_t nus_service_uuid[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e
};

static const uint8_t nus_tx_uuid[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e
};

static const uint8_t nus_rx_uuid[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e
};

static struct {
    esp_gatt_if_t gatts_if;
    uint16_t service_handle;
    uint16_t tx_char_handle;
    uint16_t tx_char_value_handle;
    uint16_t rx_char_handle;
    uint16_t rx_char_value_handle;
    uint16_t tx_cccd_handle;
    uint16_t conn_handle;
    bool connected;
    uint8_t pending_writes;
} g_ble_state = {0};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 16,
    .p_service_uuid = (uint8_t *)nus_service_uuid,
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

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            printf(">>> BLE advertising started <<<\n");
            fflush(stdout);
        }
        break;
    default:
        break;
    }
}

static void gatt_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                               esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        g_ble_state.gatts_if = gatts_if;
        printf(">>> GATT app registered <<<\n");
        fflush(stdout);
        esp_ble_gap_config_adv_data(&adv_data);
        
        // Create Nordic UART service
        esp_ble_gatts_create_service(gatts_if, &(esp_gatt_srvc_id_t){
            .is_primary = true,
            .id.inst_id = 0,
            .id.uuid.len = ESP_UUID_LEN_128,
            .id.uuid.uuid.uuid128 = {
                0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e
            }
        }, 4);
        break;
        
    case ESP_GATTS_CREATE_EVT:
        g_ble_state.service_handle = param->create.service_handle;
        printf(">>> Service created (handle=%d) <<<\n", g_ble_state.service_handle);
        fflush(stdout);
        esp_ble_gatts_start_service(g_ble_state.service_handle);
        
        // Add TX characteristic (notify)
        esp_ble_gatts_add_char(g_ble_state.service_handle,
                               &(esp_bt_uuid_t){
                                   .len = ESP_UUID_LEN_128,
                                   .uuid.uuid128 = {
                                       0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                                       0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e
                                   }
                               },
                               ESP_GATT_PERM_READ,
                               ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                               NULL, NULL);
        break;
        
    case ESP_GATTS_ADD_CHAR_EVT:
        g_ble_state.tx_char_handle = param->add_char.attr_handle;
        g_ble_state.tx_char_value_handle = param->add_char.attr_handle;
        printf(">>> TX Char added (handle=%d) <<<\n", g_ble_state.tx_char_handle);
        fflush(stdout);
        
        // Add CCCD for TX
        esp_ble_gatts_add_char_descr(g_ble_state.service_handle,
                                      &(esp_bt_uuid_t){
                                          .len = ESP_UUID_LEN_16,
                                          .uuid.uuid16 = 0x2902
                                      },
                                      ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                      NULL, NULL);
        break;
        
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        g_ble_state.tx_cccd_handle = param->add_char_descr.attr_handle;
        printf(">>> CCCD added (handle=%d) <<<\n", g_ble_state.tx_cccd_handle);
        fflush(stdout);
        
        // Add RX characteristic (write)
        esp_ble_gatts_add_char(g_ble_state.service_handle,
                               &(esp_bt_uuid_t){
                                   .len = ESP_UUID_LEN_128,
                                   .uuid.uuid128 = {
                                       0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                                       0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e
                                   }
                               },
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                               NULL, NULL);
        break;
        
    case ESP_GATTS_CONNECT_EVT:
        g_ble_state.conn_handle = param->connect.conn_id;
        g_ble_state.connected = true;
        printf(">>> BLE CLIENT CONNECTED (conn_id=%d) <<<\n", param->connect.conn_id);
        fflush(stdout);
        break;
        
    case ESP_GATTS_DISCONNECT_EVT:
        g_ble_state.connected = false;
        printf(">>> BLE CLIENT DISCONNECTED <<<\n");
        fflush(stdout);
        esp_ble_gap_start_advertising(&adv_params);
        break;
        
    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == g_ble_state.tx_cccd_handle) {
            printf(">>> CCCD written <<<\n");
            fflush(stdout);
        }
        break;
        
    default:
        break;
    }
}

static void nmea_ble_task(void *param) {
    char nmea_buffer[256];
    int nmea_len;
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    while (1) {
        if (g_ble_state.connected && traffic_received()) {
            nmea_len = nmea_format_traffic(nmea_buffer, sizeof(nmea_buffer));
            if (nmea_len > 0) {
                printf(">>> Sending NMEA (%d bytes) <<<\n", nmea_len);
                fflush(stdout);
                esp_ble_gatts_send_indicate(g_ble_state.gatts_if,
                                            g_ble_state.conn_handle,
                                            g_ble_state.tx_char_value_handle,
                                            nmea_len,
                                            (uint8_t *)nmea_buffer,
                                            false);
            }
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void nmea_ble_init(void) {
    esp_err_t ret;
    
    printf(">>> nmea_ble_init <<<\n");
    fflush(stdout);
    
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        printf("esp_bt_controller_init failed: %d\n", ret);
        fflush(stdout);
        return;
    }
    
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        printf("esp_bt_controller_enable failed: %d\n", ret);
        fflush(stdout);
        return;
    }
    
    ret = esp_bluedroid_init();
    if (ret) {
        printf("esp_bluedroid_init failed: %d\n", ret);
        fflush(stdout);
        return;
    }
    
    ret = esp_bluedroid_enable();
    if (ret) {
        printf("esp_bluedroid_enable failed: %d\n", ret);
        fflush(stdout);
        return;
    }
    
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        printf("esp_ble_gap_register_callback failed: %d\n", ret);
        fflush(stdout);
        return;
    }
    
    ret = esp_ble_gatts_register_callback(gatt_event_handler);
    if (ret) {
        printf("esp_ble_gatts_register_callback failed: %d\n", ret);
        fflush(stdout);
        return;
    }
    
    ret = esp_ble_gatts_app_register(0);
    if (ret) {
        printf("esp_ble_gatts_app_register failed: %d\n", ret);
        fflush(stdout);
        return;
    }
}

void nmea_ble_start(void) {
    printf(">>> nmea_ble_start <<<\n");
    fflush(stdout);
    xTaskCreate(nmea_ble_task, "nmea_ble_task", 4096, NULL, 5, NULL);
}

#endif
