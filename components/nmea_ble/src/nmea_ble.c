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
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatt_defs.h"
#include "traffic.h"
#include "settings.h"
#include "gps.h"
#include <stdio.h>
#include <string.h>

// Two GATT services carry the same NMEA stream so common clients can use either:
//  - HM-10 style FFE0/FFE1 (SoftRF, SkyView, Stratux): one characteristic,
//    notify + write-without-response. SkyView finds devices by FFE0 in the advert.
//  - Nordic UART 6e400001-...: TX 6e400003 (notify), RX 6e400002 (write).
//    XCSoar tries Nordic UART first, then HM-10.
// Data is sent as notifications only, split to the negotiated ATT payload.

#define DEVICE_NAME   "Stratux-ESP32-LE"
#define LOCAL_MTU     247
#define DEFAULT_MTU   23
#define NOTIFY_GAP_MS 10

#define SVC_HM10  0
#define SVC_NUS   1

#define NUS_UUID(b) { 0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
                      0x93, 0xf3, 0xa3, 0xb5, (b), 0x00, 0x40, 0x6e }

static uint8_t nus_svc_uuid[16] = NUS_UUID(0x01);
static uint8_t nus_rx_uuid[16]  = NUS_UUID(0x02);
static uint8_t nus_tx_uuid[16]  = NUS_UUID(0x03);

static uint16_t u_primary_svc = ESP_GATT_UUID_PRI_SERVICE;
static uint16_t u_char_decl   = ESP_GATT_UUID_CHAR_DECLARE;
static uint16_t u_cccd        = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static uint16_t u_ffe0        = 0xFFE0;
static uint16_t u_ffe1        = 0xFFE1;

static const uint8_t prop_hm10 = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE_NR |
                                 ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t prop_write  = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;

static uint8_t val_buf[2][20];
static uint8_t cccd_init[2] = {0x00, 0x00};

enum { HM_SVC, HM_DECL, HM_VAL, HM_CCCD, HM_N };
enum { NUS_SVC, NUS_TX_DECL, NUS_TX_VAL, NUS_CCCD, NUS_RX_DECL, NUS_RX_VAL, NUS_N };

#define AUTO_RSP { .auto_rsp = ESP_GATT_AUTO_RSP }

static const esp_gatts_attr_db_t hm10_db[HM_N] = {
    [HM_SVC]  = { AUTO_RSP, { ESP_UUID_LEN_16, (uint8_t *)&u_primary_svc, ESP_GATT_PERM_READ,
                              2, 2, (uint8_t *)&u_ffe0 } },
    [HM_DECL] = { AUTO_RSP, { ESP_UUID_LEN_16, (uint8_t *)&u_char_decl, ESP_GATT_PERM_READ,
                              1, 1, (uint8_t *)&prop_hm10 } },
    [HM_VAL]  = { AUTO_RSP, { ESP_UUID_LEN_16, (uint8_t *)&u_ffe1,
                              ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                              sizeof(val_buf[0]), 0, val_buf[0] } },
    [HM_CCCD] = { AUTO_RSP, { ESP_UUID_LEN_16, (uint8_t *)&u_cccd,
                              ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                              2, 2, cccd_init } },
};

static const esp_gatts_attr_db_t nus_db[NUS_N] = {
    [NUS_SVC]     = { AUTO_RSP, { ESP_UUID_LEN_16, (uint8_t *)&u_primary_svc, ESP_GATT_PERM_READ,
                                  16, 16, nus_svc_uuid } },
    [NUS_TX_DECL] = { AUTO_RSP, { ESP_UUID_LEN_16, (uint8_t *)&u_char_decl, ESP_GATT_PERM_READ,
                                  1, 1, (uint8_t *)&prop_notify } },
    [NUS_TX_VAL]  = { AUTO_RSP, { ESP_UUID_LEN_128, nus_tx_uuid, ESP_GATT_PERM_READ,
                                  sizeof(val_buf[1]), 0, val_buf[1] } },
    [NUS_CCCD]    = { AUTO_RSP, { ESP_UUID_LEN_16, (uint8_t *)&u_cccd,
                                  ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                  2, 2, cccd_init } },
    [NUS_RX_DECL] = { AUTO_RSP, { ESP_UUID_LEN_16, (uint8_t *)&u_char_decl, ESP_GATT_PERM_READ,
                                  1, 1, (uint8_t *)&prop_write } },
    [NUS_RX_VAL]  = { AUTO_RSP, { ESP_UUID_LEN_128, nus_rx_uuid, ESP_GATT_PERM_WRITE,
                                  20, 0, NULL } },
};

static struct {
    esp_gatt_if_t gatts_if;
    uint16_t conn_id;
    bool connected;
    uint16_t mtu;
    uint16_t hm_val_handle, hm_cccd_handle;
    uint16_t nus_tx_handle, nus_cccd_handle;
    bool hm_notify, nus_notify;
} g = {0};

// Advertisement: flags, 16-bit UUID list (FFE0) and a short complete name, so scanners that
// never receive the scan response (SkyView/NimBLE does not) still see name + service.
// Scan response: 128-bit UUID list (Nordic UART).
#define ADV_NAME "Stratux"
static uint8_t adv_raw[31];
static uint8_t scan_rsp_raw[31];
static uint8_t adv_raw_len, scan_rsp_raw_len;

static void build_adv(void) {
    size_t name_len = strlen(ADV_NAME);
    uint8_t n = 0;
    adv_raw[n++] = 2;  adv_raw[n++] = ESP_BLE_AD_TYPE_FLAG; adv_raw[n++] = 0x06;
    adv_raw[n++] = 3;  adv_raw[n++] = ESP_BLE_AD_TYPE_16SRV_CMPL;
    adv_raw[n++] = 0xE0; adv_raw[n++] = 0xFF;
    adv_raw[n++] = name_len + 1; adv_raw[n++] = ESP_BLE_AD_TYPE_NAME_CMPL;
    memcpy(&adv_raw[n], ADV_NAME, name_len); n += name_len;
    adv_raw_len = n;

    n = 0;
    scan_rsp_raw[n++] = 17; scan_rsp_raw[n++] = ESP_BLE_AD_TYPE_128SRV_CMPL;
    memcpy(&scan_rsp_raw[n], nus_svc_uuid, 16); n += 16;
    scan_rsp_raw_len = n;
}

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
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        esp_ble_gap_config_scan_rsp_data_raw(scan_rsp_raw, scan_rsp_raw_len);
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        printf(">>> BLE advertising %s <<<\n",
               param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS ? "started" : "FAILED");
        fflush(stdout);
        break;
    default:
        break;
    }
}

static void gatt_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                               esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        g.gatts_if = gatts_if;
        esp_ble_gatts_create_attr_tab(hm10_db, gatts_if, HM_N, SVC_HM10);
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK) {
            printf(">>> GATT table %d create failed: %d <<<\n",
                   param->add_attr_tab.svc_inst_id, param->add_attr_tab.status);
            fflush(stdout);
            break;
        }
        if (param->add_attr_tab.svc_inst_id == SVC_HM10) {
            g.hm_val_handle = param->add_attr_tab.handles[HM_VAL];
            g.hm_cccd_handle = param->add_attr_tab.handles[HM_CCCD];
            esp_ble_gatts_start_service(param->add_attr_tab.handles[HM_SVC]);
            esp_ble_gatts_create_attr_tab(nus_db, gatts_if, NUS_N, SVC_NUS);
        } else {
            g.nus_tx_handle = param->add_attr_tab.handles[NUS_TX_VAL];
            g.nus_cccd_handle = param->add_attr_tab.handles[NUS_CCCD];
            esp_ble_gatts_start_service(param->add_attr_tab.handles[NUS_SVC]);
            printf(">>> GATT ready (FFE0 val=%d, NUS tx=%d) <<<\n",
                   g.hm_val_handle, g.nus_tx_handle);
            fflush(stdout);
            esp_ble_gap_config_adv_data_raw(adv_raw, adv_raw_len);
        }
        break;

    case ESP_GATTS_MTU_EVT:
        g.mtu = param->mtu.mtu;
        printf(">>> BLE MTU %d <<<\n", g.mtu);
        fflush(stdout);
        break;

    case ESP_GATTS_CONNECT_EVT:
        g.conn_id = param->connect.conn_id;
        g.connected = true;
        g.mtu = DEFAULT_MTU;
        g.hm_notify = g.nus_notify = false;
        printf(">>> BLE CLIENT CONNECTED (conn_id=%d) <<<\n", g.conn_id);
        fflush(stdout);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        g.connected = false;
        g.hm_notify = g.nus_notify = false;
        printf(">>> BLE CLIENT DISCONNECTED <<<\n");
        fflush(stdout);
        esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GATTS_WRITE_EVT: {
        bool on = param->write.len == 2 && (param->write.value[0] & 0x01);
        if (param->write.handle == g.hm_cccd_handle) {
            g.hm_notify = on;
            printf(">>> FFE0 notify %s <<<\n", on ? "on" : "off");
            fflush(stdout);
        } else if (param->write.handle == g.nus_cccd_handle) {
            g.nus_notify = on;
            printf(">>> NUS notify %s <<<\n", on ? "on" : "off");
            fflush(stdout);
        }
        break;
    }

    default:
        break;
    }
}

#define NMEA_BLE_MAX_TARGETS 16

static void notify_chunks(uint16_t handle, const uint8_t *buf, size_t len, size_t chunk) {
    while (len > 0 && g.connected) {
        size_t n = len < chunk ? len : chunk;
        esp_ble_gatts_send_indicate(g.gatts_if, g.conn_id, handle, n, (uint8_t *)buf, false);
        buf += n;
        len -= n;
        vTaskDelay(pdMS_TO_TICKS(NOTIFY_GAP_MS));
    }
}

static void nmea_ble_send(const char *buf, size_t len) {
    size_t chunk = g.mtu > 3 ? g.mtu - 3 : DEFAULT_MTU - 3;
    if (g.hm_notify)
        notify_chunks(g.hm_val_handle, (const uint8_t *)buf, len, chunk);
    if (g.nus_notify)
        notify_chunks(g.nus_tx_handle, (const uint8_t *)buf, len, chunk);
}

static void nmea_ble_task(void *param) {
    char nmea_buffer[256];
    static traffic_info_t targets[NMEA_BLE_MAX_TARGETS];

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    while (1) {
        if (g.connected && (g.hm_notify || g.nus_notify)) {
            gps_ownship_t own = gps_get_ownship();
            size_t n = traffic_snapshot(targets, NMEA_BLE_MAX_TARGETS);

            size_t len = nmea_format_gprmc(&own, nmea_buffer, sizeof(nmea_buffer));
            if (len > 0)
                nmea_ble_send(nmea_buffer, len);
            len = nmea_format_gpgga(&own, nmea_buffer, sizeof(nmea_buffer));
            if (len > 0)
                nmea_ble_send(nmea_buffer, len);

            len = nmea_format_pflau(n, targets, n, &own, nmea_buffer, sizeof(nmea_buffer));
            if (len > 0)
                nmea_ble_send(nmea_buffer, len);
            for (size_t i = 0; i < n; i++) {
                len = nmea_format_pflaa(&targets[i], &own, nmea_buffer, sizeof(nmea_buffer));
                if (len > 0) {
                    nmea_ble_send(nmea_buffer, len);
                    static int cnt;
                    if (++cnt % 5 == 0)
                        ESP_LOGI("nmea_ble", "sent %.*s", (int)len - 2, nmea_buffer);
                }
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void nmea_ble_init(void) {
    esp_err_t ret;

    printf(">>> nmea_ble_init <<<\n");
    fflush(stdout);
    build_adv();

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) ||
        (ret = esp_bt_controller_enable(ESP_BT_MODE_BLE)) ||
        (ret = esp_bluedroid_init()) ||
        (ret = esp_bluedroid_enable()) ||
        (ret = esp_ble_gap_register_callback(gap_event_handler)) ||
        (ret = esp_ble_gatts_register_callback(gatt_event_handler)) ||
        (ret = esp_ble_gap_set_device_name(DEVICE_NAME)) ||
        (ret = esp_ble_gatt_set_local_mtu(LOCAL_MTU)) ||
        (ret = esp_ble_gatts_app_register(0))) {
        printf("nmea_ble_init failed: %d\n", ret);
        fflush(stdout);
    }
}

void nmea_ble_start(void) {
    printf(">>> nmea_ble_start <<<\n");
    fflush(stdout);
    xTaskCreate(nmea_ble_task, "nmea_ble_task", 4096, NULL, 5, NULL);
}

#endif
