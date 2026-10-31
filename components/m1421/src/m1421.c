#include "m1421.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pins.h"
#include "settings.h"
#include "stratux_status.h"
#include "modes.h"

static const char *TAG = "m1421";

#define M1421_UART_PORT   UART_NUM_2
#define M1421_RX_BUF      (2 * 1024)
#define M1421_LINE_MAX    512

// Beast frame format: 0x1A prefix, payload, 0x1A escape if payload contains 0x1A
#define BEAST_SYNC        0x1A
#define BEAST_DF_TYPE     1         // number of bits for DF
#define BEAST_FRAME_LEN   14        // max payload bytes for 1090ES

// Link liveness: expect frames or heartbeat at reasonable rate
#define M1421_LINK_TIMEOUT_MS 8000
static int64_t s_last_frame_ms;

// Diagnostic ring
#define DIAG_RING_SIZE 16
#define DIAG_LINE_MAX  128
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
    for (uint32_t i = 0; i < s_diag.count && written < cap; i++) {
        uint32_t idx = (s_diag.head - s_diag.count + i) % DIAG_RING_SIZE;
        const char *line = s_diag.lines[idx];
        size_t len = strlen(line);
        if (written + len + 2 > cap)
            break;
        written += snprintf(out + written, cap - written, "%s\n", line);
    }
    if (written < cap)
        out[written] = 0;
    return written;
}

// ---- Beast frame parser -------------------------------------------------------

// Convert hex ASCII digit to 4-bit value; return -1 if invalid
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Parse a single Beast frame (binary, already extracted from stream).
// Payload is the data between BEAST_SYNC bytes (escaped if needed).
// Returns true if successfully parsed, false otherwise.
static bool parse_beast_frame(const uint8_t *payload, size_t len, pong_frame_t *out)
{
    if (len < 2 || len > BEAST_FRAME_LEN)
        return false;

    // Beast 1090ES: payload[0] = DF (5 bits), payload[1..] = rest of frame
    // First byte: AAAAAAA (7 bits of DF17/18), then start of rest
    uint8_t df = (payload[0] >> 3) & 0x1F;
    
    // Only handle DF 17/18 (ADS-B)
    if (df != 17 && df != 18)
        return false;

    // Convert binary payload to hex string
    char hex_buf[PONG_HEX_MAX];
    for (size_t i = 0; i < len; i++) {
        snprintf(hex_buf + (i * 2), PONG_HEX_MAX - (i * 2), "%02X", payload[i]);
    }

    out->kind = PONG_LINE_1090ES;
    strcpy(out->hex, hex_buf);
    out->hex_len = len * 2;
    out->ss = 0;
    out->rs = 0;
    out->ss_valid = 0;

    return true;
}

// ---- Beast binary stream reader -----------------------------------------------

// Read Beast frames from UART, de-escape, and pass to parser.
// Beast frame format (from M1421 docs):
//   0x1A (sync)
//   payload (7 or 14 bytes typically)
//   0x1A (sync) — marks end
// If payload contains 0x1A, it's escaped as 0x1A 0x1A.

static bool m1421_read_frame(uart_port_t port, uint8_t *frame, size_t *frame_len, int timeout_ms)
{
    uint8_t byte;
    int len = 0;
    int64_t start_ms = esp_timer_get_time() / 1000;

    // Sync on first 0x1A
    while (len < 1000) {  // safety limit
        int bytes = uart_read_bytes(port, &byte, 1, pdMS_TO_TICKS(timeout_ms));
        if (bytes == 0) {
            if (esp_timer_get_time() / 1000 - start_ms > timeout_ms)
                return false;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (byte == BEAST_SYNC)
            break;
    }

    // Read until end 0x1A (handling escapes)
    len = 0;
    while (len < 100) {  // max frame size with some margin
        int bytes = uart_read_bytes(port, &byte, 1, pdMS_TO_TICKS(100));
        if (bytes == 0)
            return false;  // timeout waiting for frame data

        if (byte == BEAST_SYNC) {
            // End of frame
            *frame_len = len;
            return len > 0;
        }

        if (len == 0 && byte == BEAST_SYNC) {
            // Double 0x1A (escaped): add single 0x1A to payload
            frame[len++] = BEAST_SYNC;
            continue;
        }

        frame[len++] = byte;
    }

    return false;  // frame too long or no terminator
}

// ---- Baud rate detection -------------------------------------------------------

// List of baud rates to try, in order
static const uint32_t m1421_baud_rates[] = {
    9600, 19200, 38400, 57600, 115200, 230400, 460800
};
#define NUM_BAUD_RATES (sizeof(m1421_baud_rates) / sizeof(m1421_baud_rates[0]))

// Try to detect M1421 at a given baud rate: send AT probe, wait for OK
static bool m1421_probe_baud(uart_port_t port, uint32_t baud)
{
    // Configure UART at this baud rate
    uart_config_t uart_config = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(port, &uart_config);
    uart_flush_input(port);

    // Send AT command
    const char *at_cmd = "AT\r\n";
    uart_write_bytes(port, at_cmd, strlen(at_cmd));

    // Wait for "OK" response (with timeout)
    char response[64] = {0};
    int bytes_read = 0;
    int64_t start = esp_timer_get_time() / 1000;
    
    while (esp_timer_get_time() / 1000 - start < 500) {  // 500ms timeout
        int n = uart_read_bytes(port, (uint8_t *)(response + bytes_read), 
                               sizeof(response) - bytes_read - 1, pdMS_TO_TICKS(100));
        if (n > 0) {
            bytes_read += n;
            response[bytes_read] = 0;
            if (strstr(response, "OK"))
                return true;
        }
    }

    return false;
}

bool m1421_detect(void)
{
    if (!CONFIG_M1421_ENABLED)
        return false;

    ESP_LOGI(TAG, "M1421: Probing for ADS-B receiver on UART2...");

    // Install UART driver
    uart_driver_install(M1421_UART_PORT, M1421_RX_BUF, 0, 0, NULL, 0);

    // Try each baud rate
    for (size_t i = 0; i < NUM_BAUD_RATES; i++) {
        uint32_t baud = m1421_baud_rates[i];
        ESP_LOGI(TAG, "M1421: Trying %lu baud...", baud);

        if (m1421_probe_baud(M1421_UART_PORT, baud)) {
            ESP_LOGI(TAG, "M1421: Detected at %lu baud!", baud);
            diag_push("M1421 detected at %lu baud", baud);
            return true;
        }
    }

    ESP_LOGI(TAG, "M1421: Not detected on any baud rate");
    uart_driver_delete(M1421_UART_PORT);
    return false;
}

// ---- Main UART reader task ---------------------------------------------------

void m1421_rx_task(void *arg)
{
    ESP_LOGI(TAG, "M1421 task started");

    // Configure UART for Beast binary at negotiated baud
    // For now, default to 115200 (can be auto-negotiated later)
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(M1421_UART_PORT, &uart_config);
    uart_set_pin(M1421_UART_PORT, PONG_UART_TX_PIN, PONG_UART_RX_PIN, 
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    s_last_frame_ms = esp_timer_get_time() / 1000;

    while (1) {
        uint8_t frame[BEAST_FRAME_LEN];
        size_t frame_len;

        // Read next Beast frame
        if (!m1421_read_frame(M1421_UART_PORT, frame, &frame_len, 1000)) {
            // Timeout or parse error
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms - s_last_frame_ms > M1421_LINK_TIMEOUT_MS) {
                ESP_LOGW(TAG, "M1421: Link timeout, no frames received");
                g_status.m1421_connected = false;
                continue;
            }
            continue;
        }

        s_last_frame_ms = esp_timer_get_time() / 1000;
        g_status.m1421_connected = true;

        // Parse Beast frame
        pong_frame_t pong_frame = {0};
        if (!parse_beast_frame(frame, frame_len, &pong_frame)) {
            ESP_LOGV(TAG, "M1421: Failed to parse frame");
            continue;
        }

        // Dispatch to modes decoder
        diag_push("1090ES: %s", pong_frame.hex);
        modes_decode(&pong_frame);
    }
}
