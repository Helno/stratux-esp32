# ESP32-D0WD vs ESP32-S3 Pinout Comparison

**Purpose**: Document pin differences for AHRS (IMU) and Audio integration

**Status**: Template (fill in S3 details when board arrives)

## Quick Reference

| Function | ESP32-D0WD | ESP32-S3 | Status | Notes |
|----------|-----------|----------|--------|-------|
| **I2C Bus** | 0, 1 | 0, 1 | ✅ Same | Both boards support dual I2C |
| **I2C SDA (default)** | GPIO 21 | GPIO 21 | ✅ Same | Safe for IMU |
| **I2C SCL (default)** | GPIO 22 | GPIO 22 | ✅ Same | Safe for IMU |
| **IMU Address** | 0x68 | 0x68 | ✅ Same | MPU6050 standard |
| **Audio DAC** | GPIO 25, 26 | **NONE** | ❌ Different | Critical for Phase 3! |
| **I2S BCK** | GPIO 12 | GPIO 12 | ⚠️ Verify | Needed if external DAC |
| **I2S WS** | GPIO 11 | GPIO 11 | ⚠️ Verify | Needed if external DAC |
| **I2S DOUT** | GPIO 13 | GPIO 13 | ⚠️ Verify | Needed if external DAC |
| **USB Serial** | CP210x (external) | Native USB-C | ✅ S3 better | S3 has built-in USB |
| **GPIO count** | 40 (0-39) | 49 (0-48) | ✅ S3 has more | More flexibility |

---

## Phase 2: AHRS (IMU Sensor)

### I2C Configuration

**D0WD Configuration:**
```c
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define I2C_BUS I2C_NUM_1    // or I2C_NUM_0
#define IMU_ADDRESS 0x68
#define IMU_INT_PIN GPIO_NUM_xx  // TO DETERMINE
```

**S3 Configuration:**
```c
#define I2C_SDA_PIN 21       // (verify: might be 9)
#define I2C_SCL_PIN 22       // (verify: might be 8)
#define I2C_BUS I2C_NUM_1    // (verify)
#define IMU_ADDRESS 0x68
#define IMU_INT_PIN GPIO_NUM_xx  // TO DETERMINE
```

### Verification Steps (Do when S3 arrives)

```bash
# Check I2C bus 0
sudo i2cdetect -y 0

# Check I2C bus 1
sudo i2cdetect -y 1

# Look for device at 0x68 (MPU6050) or 0x69 (alternate address)
```

**Expected output if IMU is present:**
```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- --  ← MPU6050 at 0x68
70: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
```

**Fill in findings:**

- [ ] I2C Bus 0 has IMU: YES / NO
- [ ] I2C Bus 1 has IMU: YES / NO
- [ ] IMU Address: `0x__`
- [ ] IMU chip type (MPU6050 / BMI160 / other): ___________
- [ ] Physical board has I2C labeled: YES / NO
  - [ ] SDA pin number: GPIO `__`
  - [ ] SCL pin number: GPIO `__`

### Interrupt Pin Selection

IMU typically has an INT (interrupt) pin for fast motion detection.

**Safe GPIO options on both D0WD and S3:**
- GPIO 4, 5 (input-capable, no conflicts)
- GPIO 16, 17 (input-capable, no conflicts)  
- GPIO 18, 19, 20 (can be repurposed if not used for SPI)

**Avoid:**
- GPIO 0, 1, 3 (boot/serial conflicts)
- GPIO 6-11 (SPI flash)
- GPIO 12, 13 (if using I2S for audio later)
- GPIO 25, 26 (D0WD DAC, S3 free but reserve)

**Chosen INT pin**: GPIO `__` (TO DETERMINE)

---

## Phase 3: Audio Warnings

### Critical Difference: DAC

**ESP32-D0WD:**
- ✅ Has built-in 8-bit DAC on GPIO 25 and GPIO 26
- ✅ Simple: just write values to registers
- ❌ Lower audio quality (8-bit)
- ✅ No external hardware needed

**ESP32-S3:**
- ❌ NO built-in DAC
- ❌ Must use external solution
- Options below

### Audio Options for S3

#### Option 1: USB Audio (Recommended for testing)
- Device: USB speaker or USB audio interface (~$8)
- Hardware: None (just USB-C cable)
- Quality: CD-quality (16-bit, 44.1 kHz)
- Soldering: None required
- **Best for**: Early development and testing
- **How**: Write to USB audio device from firmware

#### Option 2: External I2S DAC (Recommended for production)
- Chip: MAX98357A or similar (~$3-5)
- Hardware: 3 GPIO pins (BCK, WS, DOUT) + power/ground
- Quality: 16-bit, up to 48 kHz
- Soldering: Required (1-2 hours)
- **Best for**: Final product, better audio
- **How**: I2S protocol via GPIO pins

#### Option 3: PWM + Amplifier (Budget option)
- Chip: 3.5mm amplifier module (~$2)
- Hardware: 1 GPIO pin (PWM) + power
- Quality: Analog PWM (lower fidelity)
- Soldering: Yes
- **Best for**: Simple beeps/alerts only
- **How**: GPIO PWM at ~20 kHz

### Pin Assignment for I2S (If using external DAC)

**D0WD I2S Configuration (if needed):**
```c
#define I2S_BCK_PIN GPIO_NUM_12   // Bit clock
#define I2S_WS_PIN  GPIO_NUM_11   // Word select (LRCLK)
#define I2S_DOUT_PIN GPIO_NUM_13  // Data out
```

**S3 I2S Configuration (if using external DAC):**
```c
// Option A: Standard pins
#define I2S_BCK_PIN GPIO_NUM_12   // Verify if free
#define I2S_WS_PIN  GPIO_NUM_11   // Verify if free
#define I2S_DOUT_PIN GPIO_NUM_13  // Verify if free

// Option B: Alternative pins (if above conflict)
#define I2S_BCK_PIN GPIO_NUM_42   // Alternative
#define I2S_WS_PIN  GPIO_NUM_41   // Alternative
#define I2S_DOUT_PIN GPIO_NUM_40  // Alternative
```

### Verification Steps (Do when S3 arrives)

```bash
# Check which GPIOs are already used
gpio dump

# List connected USB devices
lsusb

# Check for available USB audio
aplay -l
```

**Fill in findings:**

- [ ] S3 board has pre-installed audio hardware: YES / NO
  - [ ] Type: ___________
  - [ ] Pins used: GPIO `, GPIO `, GPIO `
- [ ] Plan for audio (select one):
  - [ ] USB speaker (develop/test, no soldering)
  - [ ] External I2S DAC (production, needs soldering)
  - [ ] PWM amplifier (budget, simple beeps only)
  - [ ] Other: ___________

---

## Comparing D0WD vs S3 for Each Phase

### Phase 1: WiFi + BLE (Complete)
- ✅ No pin conflicts
- ✅ Both chips work identically
- ✅ Same firmware for both

### Phase 2: AHRS
- ✅ I2C should work same on both
- ⚠️ Verify interrupt pin doesn't conflict
- ✅ Same code, possibly different pin #defines

### Phase 3: Audio
- ❌ MAJOR DIFFERENCE
- D0WD: Built-in DAC (GPIO 25, 26) → simple
- S3: No DAC → need external hardware
- **Recommend**: Conditional compilation or separate audio modules

### Phase 4: Terrain/Weather (Future)
- ⚠️ May need more GPIO for additional sensors
- ✅ S3 has 49 GPIO vs D0WD's 40 (slight advantage)

---

## Recommended Code Structure

### ahrs.c (IMU driver)
```c
#if defined(CONFIG_IDF_TARGET_ESP32)
    // D0WD config
    #define I2C_SDA_PIN 21
    #define I2C_SCL_PIN 22
    #define IMU_INT_PIN  4  // Example
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    // S3 config (update after verification)
    #define I2C_SDA_PIN 21
    #define I2C_SCL_PIN 22
    #define IMU_INT_PIN  4  // Or different if needed
#endif

void ahrs_init() {
    // I2C initialization using above defines
    i2c_config_t conf = {
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        // ... other config
    };
}
```

### audio.c (Audio output)
```c
#if defined(CONFIG_IDF_TARGET_ESP32)
    // D0WD: Use built-in DAC
    #include "audio_dac.h"
    audio_init_dac();  // GPIO 25, 26
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    // S3: Use external hardware
    #ifdef CONFIG_AUDIO_USE_USB
        #include "audio_usb.h"
        audio_init_usb();
    #else
        #include "audio_i2s.h"
        audio_init_i2s();  // GPIO 12, 11, 13
    #endif
#endif
```

---

## Testing Procedure

### Before soldering anything:

1. **Verify pins are free:**
   - Run `gpio dump` to see what's already used
   - Avoid GPIO 0, 1, 3 (boot pins)
   - Avoid GPIO 6-11 (SPI flash)

2. **Test with I2C scanner:**
   - Run `sudo i2cdetect -y 0` and `-y 1`
   - Confirms I2C works before adding IMU

3. **Document what you find:**
   - Update this file with real pinouts
   - Create `docs/S3_VERIFIED_PINOUT.md` once tested

### When adding hardware:

1. **Add one sensor at a time**
   - Test IMU before thinking about audio
   - Don't solder everything at once

2. **Update firmware incrementally**
   - Get I2C working first
   - Then add Kalman filter
   - Then add BLE output

3. **Commit after each milestone:**
   ```bash
   git commit -m "Verified IMU I2C on S3 bus 1, address 0x68"
   ```

---

## Gotchas and Lessons Learned

### Gotcha 1: I2C address conflicts
- If you have multiple I2C devices, they need different addresses
- Solution: Check all devices with `i2cdetect` before adding hardware

### Gotcha 2: GPIO pin states at boot
- Some GPIO pins are strapped at boot (GPIO 0, 2, 15, etc.)
- Don't use these for critical functions
- Solution: Use GPIO 4, 5, 16-20 for safe choices

### Gotcha 3: Interrupt pin jitter
- Sensor interrupts can cause WiFi/BLE latency
- Solution: Use high-priority FreeRTOS task for IMU

### Gotcha 4: I2S conflicts with WiFi
- I2S and WiFi can interfere if they share DMA
- Solution: Configure DMA carefully in sdkconfig
- This is handled by ESP-IDF defaults, but monitor for issues

### Gotcha 5: S3 USB-C gotcha
- S3 has native USB but some boards still have QT Py pinout
- Verify your board type before designing USB audio integration

---

## Summary

### What's guaranteed to work on both D0WD and S3:
- ✅ WiFi + BLE (already working)
- ✅ I2C on GPIO 21/22 (for IMU)
- ✅ GPIO 4, 5, 16-20 (for interrupts)

### What's different:
- ❌ Audio DAC: D0WD has built-in (GPIO 25/26), S3 doesn't
- ⚠️ I2S pins: May differ between boards
- ✅ Total GPIO: S3 has 49 vs D0WD's 40 (advantage S3)

### Next action when S3 arrives:
1. Run `i2cdetect` to confirm IMU location
2. Update this document with actual findings
3. Create `docs/S3_VERIFIED_PINOUT.md` with confirmed pins
4. Update `components/ahrs/` with correct #defines
5. Proceed with Phase 2 (AHRS implementation)

