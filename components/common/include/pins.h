#pragma once
#include "driver/gpio.h"
#include "driver/uart.h"             // UART_NUM_2

// TTGO T8 (LilyGO) pin map. ESP32 GPIO is heavily multiplexed and the T8
// reserves pins for SD, PSRAM, and flash; this map is deliberately
// conflict-free for the Pong UART.
//
// ⚠️ CRITICAL: ESP32-D0WD and ESP32-S3 have COMPLETELY DIFFERENT GPIO assignments!
// Use conditional compilation (#if CONFIG_IDF_TARGET_*) to support both chips.
// This allows one firmware to work on both boards.

#if defined(CONFIG_IDF_TARGET_ESP32)
    // ===== TTGO T8 with ESP32-D0WD (original) =====
    
    // --- Pong radio — UART2 @ 3 Mbaud, 8N1, static RTS ---
    #define PONG_UART_PORT   UART_NUM_2
    #define PONG_UART_BAUD   3000000
    #define PONG_RX_GPIO     GPIO_NUM_35   // input-only; NO internal pull — Pong must drive idle-high
    #define PONG_TX_GPIO     GPIO_NUM_33   // RTC GPIO, free output
    #define PONG_RTS_GPIO    GPIO_NUM_32   // RTC GPIO; static level, NOT HW flow control
    
    // --- GPS — UART1 @ 9600 baud (auto-detect) ---
    #define GPS_UART_PORT    UART_NUM_1
    #define GPS_RX_GPIO      GPIO_NUM_34   // input-only (RX)
    #define GPS_TX_GPIO      GPIO_NUM_4    // standard output (config commands)
    
    // --- I2C for future milestones (Phase 2 AHRS, etc.) ---
    #define I2C_SDA_GPIO     GPIO_NUM_21
    #define I2C_SCL_GPIO     GPIO_NUM_22

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    // ===== Freenove ESP32-S3 =====
    // ⚠️ GPIO PINS ARE COMPLETELY DIFFERENT FROM D0WD!
    // Actual connections on this board:
    
    // --- Pong radio — UART2 @ 3 Mbaud, 8N1, static RTS ---
    #define PONG_UART_PORT   UART_NUM_2
    #define PONG_UART_BAUD   3000000
    #define PONG_RX_GPIO     GPIO_NUM_10   // Connected to Pong RX
    #define PONG_TX_GPIO     GPIO_NUM_11   // Unused TX (Pong RX-only); reserved for future
    #define PONG_RTS_GPIO    GPIO_NUM_7    // Static RTS level control (RTC-capable)
    
    // --- GPS — UART1 @ 9600 baud (auto-detect) ---
    #define GPS_UART_PORT    UART_NUM_1
    #define GPS_RX_GPIO      GPIO_NUM_8    // Connected to GPS RX
    #define GPS_TX_GPIO      GPIO_NUM_9    // Connected to GPS TX
    
    // --- I2C for future milestones (Phase 2 AHRS, etc.) ---
    // ✅ SAME on S3 as D0WD — no change needed!
    #define I2C_SDA_GPIO     GPIO_NUM_21
    #define I2C_SCL_GPIO     GPIO_NUM_22

#else
    #error "Unsupported ESP32 variant; add GPIO pin mapping for your board (see docs/PIN_MAPPING_D0WD_VS_S3.md)"

#endif

// --- Console — UART0 (USB-serial bridge); leave alone ---
//   TX=GPIO1  RX=GPIO3

// --- Reserved by hardware (both chips) — do NOT reuse ---
//   GPIO16/17 = PSRAM data (Bug: uart_set_pin silently strands a UART here)
//   GPIO6-11  = internal flash
//   GPIO14/15/2 (+13) = microSD SDMMC 1-bit, if the slot is populated
//   strapping pins: 0, 2, 5, 12, 15  (avoid for new outputs)
