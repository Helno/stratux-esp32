#include "m1421.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include "cJSON.h"
#include "pins.h"
#include "settings.h"
#include "stratux_status.h"
#include "traffic.h"
#include "uat.h"

// ADSBee m1421 dual-band (1090ES + 978 UAT) receiver on its console UART. It uses the
// Pong's UART2 pins (the two are never connected together; settings pick one).
//
// The console runs at a fixed 1,000,000 baud, CRLF-terminated AT commands. With
// AT+PROTOCOL_OUT=CONSOLE,AIRCRAFT_JSON_UAT_UPLINK the module sends one JSON object per line:
//   {"hex":"a63993","type":"adsb_icao","link":"1090","lat":..,"lon":..,"alt_baro":..,...}
//   {"uat_uplink":"<576 base64 chars>","rssi":-80,"t":123456}
// Aircraft are already decoded by the module; an uplink is the de-interleaved, error-corrected
// 432 byte FIS-B payload, the same thing the Pong delivers as a '+' line.

static const char *TAG = "m1421";

#define M1421_UART_PORT    PONG_UART_PORT
#define M1421_BAUD         1000000
#define M1421_RX_BUF       4096
#define M1421_LINE_MAX     1024            // longest line is ~621 bytes (an uplink)
#define M1421_KEEPALIVE_MS 30000
#define M1421_FIRST_RETRY_MS 3000          // module may still be booting at the first send
// The module only prints when it hears aircraft, but it answers each keepalive with "OK".
#define M1421_LINK_TIMEOUT_MS 45000
#define UPLINK_BYTES       432

static const char *const s_commands[] = {
    "AT+LOG_LEVEL=SILENT",
    "AT+PROTOCOL_OUT=CONSOLE,AIRCRAFT_JSON_UAT_UPLINK",
};

static int64_t s_last_line_ms;
static uint32_t s_rx_bytes, s_lines_json, s_lines_other;

// Raw feed ring: the last lines received, truncated (long uplinks are cut).
#define DIAG_RING_SIZE 16
#define DIAG_LINE_MAX  192
typedef struct {
    char lines[DIAG_RING_SIZE][DIAG_LINE_MAX];
    uint32_t head;
    uint32_t count;
} diag_ring_t;
static diag_ring_t s_diag;

static void diag_push(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_diag.lines[s_diag.head], DIAG_LINE_MAX, fmt, args);
    va_end(args);
    s_diag.head = (s_diag.head + 1) % DIAG_RING_SIZE;
    if (s_diag.count < DIAG_RING_SIZE)
        s_diag.count++;
}

size_t m1421_diag_copy(char *out, size_t cap)
{
    size_t written = 0;
    for (uint32_t i = 0; i < s_diag.count; i++) {
        uint32_t idx = (s_diag.head + DIAG_RING_SIZE - s_diag.count + i) % DIAG_RING_SIZE;
        const char *line = s_diag.lines[idx];
        size_t len = strlen(line);
        if (written + len + 1 >= cap)
            break;
        memcpy(out + written, line, len);
        written += len;
        out[written++] = '\n';
    }
    if (written < cap)
        out[written] = '\0';
    return written;
}

static void send_commands(void)
{
    for (size_t i = 0; i < sizeof(s_commands) / sizeof(s_commands[0]); i++) {
        uart_write_bytes(M1421_UART_PORT, s_commands[i], strlen(s_commands[i]));
        uart_write_bytes(M1421_UART_PORT, "\r\n", 2);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ---- Aircraft ----------------------------------------------------------------

static traffic_addr_type_t addr_type_of(const char *type)
{
    if (!type) return ADDR_TYPE_ADSB_ICAO;
    if (!strcmp(type, "adsb_other")) return ADDR_TYPE_ADSB_OTHER;
    if (!strcmp(type, "tisb_icao") || !strncmp(type, "adsr_", 5)) return ADDR_TYPE_TISB_ICAO;
    if (!strcmp(type, "tisb_trackfile") || !strcmp(type, "tisb_other")) return ADDR_TYPE_TISB_OTHER;
    return ADDR_TYPE_ADSB_ICAO;
}

// readsb "A0".."D7" -> GDL90 emitter category (A7 = 0x07, B0 = 0x08, ...).
static bool emitter_cat_of(const char *c, uint8_t *out)
{
    if (!c || strlen(c) != 2 || c[0] < 'A' || c[0] > 'D' || c[1] < '0' || c[1] > '7')
        return false;
    *out = (uint8_t)((c[0] - 'A') * 8 + (c[1] - '0'));
    return true;
}

static bool num(const cJSON *j, const char *key, double *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsNumber(v))
        return false;
    *out = v->valuedouble;
    return true;
}

static void handle_aircraft(const cJSON *j, const char *hex)
{
    bool non_icao = (hex[0] == '~');
    const char *digits_start = non_icao ? hex + 1 : hex;
    char *end = NULL;
    unsigned long addr = strtoul(digits_start, &end, 16);
    if (end == digits_start || *end != '\0' || addr > 0xFFFFFF)
        return;

    const cJSON *jtype = cJSON_GetObjectItemCaseSensitive(j, "type");
    const cJSON *jlink = cJSON_GetObjectItemCaseSensitive(j, "link");
    bool is978 = cJSON_IsString(jlink) && !strcmp(jlink->valuestring, "978");

    traffic_info_t t;
    memset(&t, 0, sizeof(t));
    t.icao_addr = (uint32_t)addr;
    t.addr_type = non_icao ? ADDR_TYPE_ADSB_OTHER
                           : addr_type_of(cJSON_IsString(jtype) ? jtype->valuestring : NULL);
    t.src = is978 ? TRAFFIC_SRC_UAT : TRAFFIC_SRC_ES;

    double v, lon;
    // The module repeats the last known position with every update, so only a recent one counts.
    double seen_pos = 0;
    bool pos_fresh = !num(j, "seen_pos", &seen_pos) || seen_pos <= 2;
    if (num(j, "lat", &v) && num(j, "lon", &lon) && (v != 0 || lon != 0) && pos_fresh) {
        t.position_valid = true;
        t.lat = v;
        t.lng = lon;
    }

    // alt_baro is a number, or the string "ground" in readsb output.
    const cJSON *jbaro = cJSON_GetObjectItemCaseSensitive(j, "alt_baro");
    if (cJSON_IsNumber(jbaro)) {
        t.alt_ft = (int32_t)jbaro->valuedouble;
        t.alt_valid = true;
    } else if (cJSON_IsString(jbaro) && !strcmp(jbaro->valuestring, "ground")) {
        t.on_ground = true;
        t.airground_valid = true;
    }
    if (num(j, "alt_geom", &v)) {
        t.gnss_alt_ft = (int32_t)v;
        t.gnss_alt_valid = true;
    }

    double track;
    if (num(j, "gs", &v) && (num(j, "track", &track) || num(j, "true_heading", &track))) {
        t.speed_kt = (uint16_t)v;
        int trk = (int)track % 360;
        t.track_deg = (uint16_t)(trk < 0 ? trk + 360 : trk);
        t.speed_valid = true;
    }
    if (num(j, "baro_rate", &v) || num(j, "geom_rate", &v)) {
        t.vvel_fpm = (int16_t)v;
        t.vvel_valid = true;
    }

    const cJSON *jflight = cJSON_GetObjectItemCaseSensitive(j, "flight");
    if (cJSON_IsString(jflight)) {
        const char *s = jflight->valuestring;
        while (*s == ' ') s++;
        size_t n = strlen(s);
        while (n > 0 && s[n - 1] == ' ') n--;
        if (n > 0) {
            if (n > sizeof(t.tail) - 1) n = sizeof(t.tail) - 1;
            memcpy(t.tail, s, n);
            t.tail[n] = '\0';
            t.tail_valid = true;
        }
    }

    const cJSON *jcat = cJSON_GetObjectItemCaseSensitive(j, "category");
    if (cJSON_IsString(jcat) && emitter_cat_of(jcat->valuestring, &t.emitter_cat))
        t.cat_valid = true;

    if (num(j, "nic", &v)) {
        t.nic = (uint8_t)v;
        t.nic_valid = true;
    }
    if (num(j, "nac_p", &v) && v > 0) {
        t.nacp = (uint8_t)v;
        t.nacp_valid = true;
    }

    // rssi is INT16_MIN ("no value") for 1090 lines, so signal strength is left unset.

    if (!(t.position_valid || t.alt_valid || t.gnss_alt_valid || t.speed_valid ||
          t.vvel_valid || t.tail_valid || t.cat_valid || t.airground_valid))
        return;

    if (is978)
        g_status.uat_msgs++;
    else
        g_status.es_msgs++;
    traffic_upsert(&t);
}

// ---- UAT uplink ---------------------------------------------------------------

static void handle_uplink(const char *b64)
{
    uint8_t frame[UPLINK_BYTES];
    size_t olen = 0;
    if (mbedtls_base64_decode(frame, sizeof(frame), &olen,
                              (const unsigned char *)b64, strlen(b64)) != 0 ||
        olen != UPLINK_BYTES) {
        g_status.uat_rejected++;
        return;
    }

    static pong_frame_t f;   // too large for the task stack
    memset(&f, 0, sizeof(f));
    f.kind = PONG_LINE_UAT_UP;
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < UPLINK_BYTES; i++) {
        f.hex[2 * i]     = digits[frame[i] >> 4];
        f.hex[2 * i + 1] = digits[frame[i] & 0x0F];
    }
    f.hex_len = 2 * UPLINK_BYTES;
    f.hex[f.hex_len] = '\0';
    uat_decode_frame(&f);
}

// ---- Line handling -------------------------------------------------------------

static void handle_line(char *line, size_t len)
{
    s_last_line_ms = esp_timer_get_time() / 1000;
    g_status.m1421_connected = true;

    if (len == 0)
        return;
    // Lines that may carry key material are never kept.
    if (!strstr(line, "Key"))
        diag_push("%s", line);
    if (line[0] != '{') {
        if (s_lines_other < 10 && !strstr(line, "Key"))
            ESP_LOGI(TAG, "module: %s", line);
        s_lines_other++;
        return;
    }
    s_lines_json++;
    cJSON *j = cJSON_ParseWithLength(line, len);
    if (!j) {
        ESP_LOGV(TAG, "bad JSON line (%u bytes)", (unsigned)len);
        return;
    }
    const cJSON *uplink = cJSON_GetObjectItemCaseSensitive(j, "uat_uplink");
    const cJSON *hex = cJSON_GetObjectItemCaseSensitive(j, "hex");
    if (cJSON_IsString(uplink))
        handle_uplink(uplink->valuestring);
    else if (cJSON_IsString(hex))
        handle_aircraft(j, hex->valuestring);
    cJSON_Delete(j);
}

// ---- Main UART reader task -----------------------------------------------------

void m1421_rx_task(void *arg)
{
    ESP_LOGI(TAG, "M1421 task started: UART%d RX=GPIO%d TX=GPIO%d @ %d baud",
             M1421_UART_PORT, PONG_RX_GPIO, PONG_TX_GPIO, M1421_BAUD);

    uart_config_t cfg = {
        .baud_rate = M1421_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(M1421_UART_PORT, M1421_RX_BUF, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(M1421_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(M1421_UART_PORT, PONG_TX_GPIO, PONG_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    static char line[M1421_LINE_MAX];
    size_t len = 0;
    bool overflow = false;
    int64_t start_ms = esp_timer_get_time() / 1000;
    bool retried = false;
    s_last_line_ms = start_ms;

    // The module forgets unsaved settings on reset, so reassert them periodically.
    send_commands();
    int64_t last_cmd_ms = esp_timer_get_time() / 1000;

    while (1) {
        uint8_t buf[128];
        int n = uart_read_bytes(M1421_UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n > 0 && s_rx_bytes < 64) {
            char hx[3 * 32 + 1];
            int m = n < 32 ? n : 32;
            for (int i = 0; i < m; i++)
                snprintf(hx + 3 * i, 4, "%02x ", buf[i]);
            ESP_LOGI(TAG, "raw rx (%d): %s", n, hx);
        }
        for (int i = 0; i < n; i++) {
            s_rx_bytes++;
            char c = (char)buf[i];
            if (c == '\n') {
                if (!overflow) {
                    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
                        len--;
                    line[len] = '\0';
                    handle_line(line, len);
                }
                len = 0;
                overflow = false;
            } else if (len < M1421_LINE_MAX - 1) {
                line[len++] = c;
            } else {
                overflow = true;
            }
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
        if (!retried && now_ms - start_ms >= M1421_FIRST_RETRY_MS) {
            retried = true;
            send_commands();
            last_cmd_ms = now_ms;
        } else if (now_ms - last_cmd_ms >= M1421_KEEPALIVE_MS) {
            send_commands();
            last_cmd_ms = now_ms;
        }
        static int64_t last_stats_ms;
        if (now_ms - last_stats_ms >= 30000) {
            last_stats_ms = now_ms;
            ESP_LOGI(TAG, "rx bytes=%u json=%u other=%u connected=%d",
                     (unsigned)s_rx_bytes, (unsigned)s_lines_json,
                     (unsigned)s_lines_other, g_status.m1421_connected);
        }
        if (g_status.m1421_connected && now_ms - s_last_line_ms > M1421_LINK_TIMEOUT_MS) {
            ESP_LOGW(TAG, "no data from the M1421 for %d s", M1421_LINK_TIMEOUT_MS / 1000);
            g_status.m1421_connected = false;
        }
    }
}
