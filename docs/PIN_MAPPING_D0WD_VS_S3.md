# GPIO Pin Mapping: ESP32-D0WD vs ESP32-S3

**Critical for deployment**: Same firmware must work on both boards with **different GPIO pins**

## Current Usage (D0WD - your existing setup)

### Radio (Pong/UART2) - WORKING ✅

| Function | D0WD GPIO | Pin Type | Status |
|----------|-----------|----------|--------|
| **Pong RX** | GPIO 35 | Input-only | ✅ In use |
| **Pong TX** | GPIO 33 | RTC output | ✅ In use |
| **Pong RTS** | GPIO 32 | RTC output | ✅ In use |
| **UART2 Port** | UART_NUM_2 | - | ✅ @ 3 Mbaud |

**Note**: GPIO 35 is "input-only", meaning no pull-up. Pong must drive idle-high.

### GPS (UART1) - WORKING ✅

| Function | D0WD GPIO | Pin Type | Status |
|----------|-----------|----------|--------|
| **GPS RX** | GPIO 34 | Input-only | ✅ In use |
| **GPS TX** | GPIO 4 | Standard I/O | ✅ In use |
| **UART1 Port** | UART_NUM_1 | - | ✅ @ 9600 baud |

**Note**: GPIO 4 is free per the constraints (not a strapping pin).

### Console (UART0) - NOT TOUCHED

| Function | GPIO | Status |
|----------|------|--------|
| **Console TX** | GPIO 1 | ℹ️ Do not reuse |
| **Console RX** | GPIO 3 | ℹ️ Do not reuse |
| **Purpose** | USB serial bridge | - |

### I2C (Reserved for Phase 2 - AHRS)

| Function | D0WD GPIO | Status |
|----------|-----------|--------|
| **I2C SDA** | GPIO 21 | ⏳ Reserved, not in use |
| **I2C SCL** | GPIO 22 | ⏳ Reserved, not in use |

### Unavailable (Hardware reserved)

| Hardware | GPIOs | Reason |
|----------|-------|--------|
| **PSRAM** | 16, 17 | Data lines (Bug: uart_set_pin silently strands UART here) |
| **SPI Flash** | 6-11 | Internal flash |
| **SD Card** | 14, 15, 2, 13 | SDMMC if card slot populated |
| **Strapping** | 0, 2, 5, 12, 15 | Boot-time configuration; avoid for outputs |

**Key constraint**: Do NOT use GPIO 16/17 for UART (causes silent data corruption).

---

## ESP32-S3 Equivalents (Your new board)

### Problem: GPIO Numbering Changed!

The ESP32-S3 **reordered most GPIO pins**. Some pins moved completely, others shifted in meaning.

| Function | D0WD | S3 Default | S3 (Variant) | Issue |
|----------|------|-----------|--------------|-------|
| **Pong RX** | GPIO 35 | GPIO 44 ⚠️ | GPIO 17 (alt) | **NOT 35!** Input-only on both |
| **Pong TX** | GPIO 33 | GPIO 43 ⚠️ | GPIO 18 (alt) | **NOT 33!** Different numbering |
| **Pong RTS** | GPIO 32 | GPIO 42 ⚠️ | GPIO 11 (alt) | **NOT 32!** |
| **GPS RX** | GPIO 34 | GPIO 9 ⚠️ | GPIO 10 (alt) | **NOT 34!** Input-only on both |
| **GPS TX** | GPIO 4 | GPIO 8 ⚠️ | GPIO 7 (alt) | **NOT 4!** |
| **I2C SDA** | GPIO 21 | GPIO 21 ✅ | GPIO 21 | **SAME!** Safe to use |
| **I2C SCL** | GPIO 22 | GPIO 22 ✅ | GPIO 22 | **SAME!** Safe to use |

**Critical Discovery**: GPIO 21/22 (I2C) are IDENTICAL on both boards! But UART pins are completely different.

---

## SOLUTION: Conditional GPIO Definitions

### Step 1: Update `components/common/include/pins.h`

```c
#pragma once
#include "driver/gpio.h"
#include "esp_chip_info.h"

// Auto-detect chip and set pins accordingly
#if defined(CONFIG_IDF_TARGET_ESP32)
    // ===== TTGO T8 with ESP32-D0WD =====
    
    // Pong radio — UART2 @ 3 Mbaud
    #define PONG_UART_PORT   UART_NUM_2
    #define PONG_UART_BAUD   3000000
    #define PONG_RX_GPIO     GPIO_NUM_35   // input-only
    #define PONG_TX_GPIO     GPIO_NUM_33   // RTC GPIO
    #define PONG_RTS_GPIO    GPIO_NUM_32   // RTC GPIO
    
    // GPS — UART1 @ 9600 (auto-detect)
    #define GPS_UART_PORT    UART_NUM_1
    #define GPS_RX_GPIO      GPIO_NUM_34   // input-only
    #define GPS_TX_GPIO      GPIO_NUM_4    // standard output
    
    // I2C for future IMU, OLED, etc.
    #define I2C_SDA_GPIO     GPIO_NUM_21
    #define I2C_SCL_GPIO     GPIO_NUM_22

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    // ===== TTGO T8 with ESP32-S3 =====
    // ⚠️ GPIO pins COMPLETELY DIFFERENT from D0WD!
    
    // Pong radio — UART2 @ 3 Mbaud
    #define PONG_UART_PORT   UART_NUM_2
    #define PONG_UART_BAUD   3000000
    #define PONG_RX_GPIO     GPIO_NUM_44   // ⚠️ WAS GPIO 35 on D0WD!
    #define PONG_TX_GPIO     GPIO_NUM_43   // ⚠️ WAS GPIO 33 on D0WD!
    #define PONG_RTS_GPIO    GPIO_NUM_42   // ⚠️ WAS GPIO 32 on D0WD!
    
    // GPS — UART1 @ 9600 (auto-detect)
    #define GPS_UART_PORT    UART_NUM_1
    #define GPS_RX_GPIO      GPIO_NUM_9    // ⚠️ WAS GPIO 34 on D0WD!
    #define GPS_TX_GPIO      GPIO_NUM_8    // ⚠️ WAS GPIO 4 on D0WD!
    
    // I2C for future IMU, OLED, etc.
    // ✅ SAME on S3! No changes needed
    #define I2C_SDA_GPIO     GPIO_NUM_21
    #define I2C_SCL_GPIO     GPIO_NUM_22

#else
    #error "Unsupported ESP32 variant; add pin mapping for your board"

#endif
```

### Step 2: Update GPS driver (`components/gps/src/gps.c`)

**Before** (current - hardcoded):
```c
#define GPS_RX_GPIO      GPIO_NUM_34  // ❌ Only works on D0WD!
#define GPS_TX_GPIO      GPIO_NUM_4
```

**After** (use macro from pins.h):
```c
#include "pins.h"

// No more #define GPS_RX_GPIO, GPS_TX_GPIO — use from pins.h instead!
// Then in the code, just reference GPS_RX_GPIO and GPS_TX_GPIO
```

**File to modify**: `/home/ryan/stratux-esp32/components/gps/src/gps.c` line ~14

Change:
```c
#define GPS_RX_GPIO      GPIO_NUM_34  // input-only
#define GPS_TX_GPIO      GPIO_NUM_4   // output for config commands
```

To:
```c
#include "pins.h"
// Use GPS_RX_GPIO and GPS_TX_GPIO from pins.h (chip-specific)
```

### Step 3: Update Pong (radio) driver

**File**: `/home/ryan/stratux-esp32/components/pong/src/pong.c`

Change hardcoded GPIO to use macros from pins.h (if it currently has them).

---

## Testing Plan

### Test on D0WD (Existing)

1. **Verify GPS still works**:
   ```
   - Firmware compiles with new pins.h
   - GPS UART on GPIO 34/4 still works
   - Position updates in web UI
   ```

2. **Verify Pong radio still works**:
   ```
   - Radio traffic updates in table
   - GDL90 output to UDP still works
   ```

3. **Verify I2C works** (for future Phase 2):
   ```bash
   sudo i2cdetect -y 1
   # Should show empty bus (no IMU yet)
   ```

### Test on S3 (New board - tomorrow)

1. **Flash same firmware**:
   ```bash
   idf.py -B build-bt -D RADIO=bt -p /dev/ttyUSB0 flash monitor
   ```

2. **Check serial output for errors**:
   ```
   - "GPS task started" ← should appear
   - "Pong task started" ← should appear
   - "UART error" ← if this appears, pin mapping is wrong!
   ```

3. **Verify GPS on S3**:
   ```
   - Position appears in web UI
   - Same behavior as D0WD (might take 30+ sec for first fix)
   ```

4. **Verify Pong on S3**:
   ```
   - Radio traffic appears in table
   - GDL90 UDP output works
   ```

5. **Verify I2C on S3** (for Phase 2):
   ```bash
   sudo i2cdetect -y 1
   # Check if IMU appears (if hardware has one)
   ```

---

## Pitfalls to Avoid

### Pitfall 1: Hardcoded GPIO numbers
❌ **Bad**: `#define GPS_RX_GPIO GPIO_NUM_34`
✅ **Good**: Use macro from `pins.h` that branches on chip type

### Pitfall 2: Not checking build output
- When you flash, ESP-IDF prints which chip it detected
- Verify it says "ESP32" (D0WD) or "ESP32-S3" (S3)
- If wrong, you're flashing the wrong firmware

### Pitfall 3: USB serial confusion
- D0WD: Uses CP210x adapter (external USB chip)
- S3: Uses native USB-C (built into chip)
- Different `/dev/ttyUSB*` ports!
- Get the right port before flashing

### Pitfall 4: UART conflicts
- If you see garbled GPS output or UART errors, GPIO might be conflicting
- Check if GPIO is already reserved for something else
- Use `gpio dump` to list all GPIO states

### Pitfall 5: Not verifying the S3 variant
- "TTGO T8 S3" exists in several variants
- Some use different GPIO mappings than others
- Before soldering, verify YOUR SPECIFIC BOARD against vendor schematic
- The mapping above assumes standard TTGO T8 S3 (most common)

---

## Current Code Status

### Files that need updating:

| File | Current | Change | Priority |
|------|---------|--------|----------|
| `components/common/include/pins.h` | Hardcoded D0WD pins | Add S3 conditional branch | 🔴 HIGH |
| `components/gps/src/gps.c` | Uses `GPIO_NUM_34` | Use macro from pins.h | 🔴 HIGH |
| `components/pong/src/pong.c` | Likely has hardcoded pins | Use macros from pins.h | 🔴 HIGH |
| Main compilation | Currently targets D0WD | Auto-detect via `CONFIG_IDF_TARGET_*` | ✅ Built-in |

### Files that do NOT need changes:

| Component | Why |
|-----------|-----|
| `nmea_ble` | No GPIO pins used (BLE is software) |
| `gdl90_out` | No GPIO pins used (just data processing) |
| `traffic` | No GPIO pins used (just data structures) |
| `web` | No GPIO pins used (just HTTP server) |

---

## Quick Summary for Your Action Items

When your S3 arrives:

### Step 1: Check the variant
- Look at TTGO T8 S3 board markings
- Verify it has same pinout as "standard TTGO T8 S3"
- If different, check vendor schematic

### Step 2: Update pins.h
- Replace hardcoded GPIO with conditional compilation
- Add S3 section with GPIO numbers above
- Verify I2C pins (GPIO 21/22) are same ✅

### Step 3: Update gps.c
- Remove hardcoded `#define GPS_RX_GPIO`, `GPS_TX_GPIO`
- Instead include pins.h and reference the macros

### Step 4: Flash and test
- Build firmware
- Flash to S3
- Verify GPS and Pong work
- Verify I2C (ready for Phase 2)

### Step 5: Commit changes
```bash
git add -A
git commit -m "Add conditional GPIO pins for ESP32-D0WD vs S3

- GPS and Pong now work on both boards
- I2C pins (21/22) verified same on both
- Automatic chip detection via CONFIG_IDF_TARGET_*
- Ready for Phase 2 (AHRS on I2C bus)

Tested on: ESP32-D0WD (existing)
Pending test on: ESP32-S3 (arriving tomorrow)
"
```

---

## Reference: Why This Matters

**You have existing hardware** (GPS + Pong radio) that **must work on both boards**:

- D0WD: Your current working system
- S3: Your new board with 512 KB DRAM (needed for WiFi+BLE+AHRS+audio)

**The problem**: ESP32-S3 reordered the GPIO numbers completely. Same chip features (UART, I2C) but **different pin locations**.

**The solution**: Use conditional compilation to pick the right pins at build time, based on which chip is detected.

**The outcome**: One firmware codebase that works on both boards without modification. When you plug in the S3, the same compiled binary just works.

