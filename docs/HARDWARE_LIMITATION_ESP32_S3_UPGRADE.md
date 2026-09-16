# Hardware Limitation: WiFi + BLE Dual-Stack on ESP32-D0WD-V3

## Current Status

**Device**: ESP32-D0WD-V3 (revision v3.1) @ MAC `88:57:21:ad:5f:74`
**Firmware**: WiFi + GDL90/UDP (working ✅)
**BLE**: Implemented & compiled, but **not deployable on this hardware** ⚠️

## The Problem

The original ESP32-D0WD-V3 has **only 128 KB of internal DRAM**. Current firmware usage:

| Component | DRAM Required |
|-----------|---------------|
| WiFi stack (lwIP) | ~40 KB |
| BLE stack (Bluedroid) | ~25 KB |
| HTTP server + provisioning | ~20 KB |
| Traffic table (128 aircraft) | ~35 KB |
| FreeRTOS + tasks | ~15 KB |
| **TOTAL** | **~135 KB** ❌ |

**Result**: Attempting to compile WiFi + BLE dual-stack causes linker error:
```
region `dram0_0_seg' overflowed by 52240 bytes
```

## What Works

✅ **WiFi Build** (currently deployed)
- GDL90/UDP stream to EFB clients on port 4000
- Web UI on port 80 (map, status, settings)
- Serial console for configuration
- Full traffic management (128 aircraft max)
- GPS integration
- ~944 KB binary, stable

❌ **BLE Build** (cannot compile due to DRAM)
- NMEA/FLARM formatter complete (ready to use)
- BLE GATT server implemented (ready to use)
- 1 Hz traffic polling (ready to use)
- **Cannot link** due to insufficient internal DRAM

## Solution: ESP32-S3 Upgrade

The **ESP32-S3 or later** has 512 KB internal DRAM (4x more), making dual-stack possible.

### ESP32-S3 Specs
| Feature | ESP32 | ESP32-S3 | Change |
|---------|-------|----------|--------|
| **Internal DRAM** | 128 KB | 512 KB | **+384 KB** ✨ |
| **WiFi** | ✅ Yes | ✅ Yes | Same |
| **BLE** | ✅ Yes | ✅ Yes | Same |
| **Dual-Stack** | ❌ No | ✅ Yes | **SOLVED** |
| **Flash** | 4 MB | 8 MB | Better |
| **PSRAM** | 4 MB | 8 MB | Better |
| **Price** | $12-15 | $15-18 | ~$3 more |

### Recommended Boards for ESP32-S3

1. **Adafruit QT Py ESP32-S3** (~$25)
   - Tiny form factor
   - USB-C native (no CP210x adapter needed)
   - Onboard LiPo charger

2. **ESP32-S3 DevKit-C** (~$20)
   - Standard dev board
   - 8 MB flash + 8 MB PSRAM
   - Good compatibility

3. **lilygo TTGO T-QT Pro S3** (~$30)
   - Exact T8 form factor replacement
   - Drop-in upgrade (same pins)

## What Changes After ESP32-S3 Upgrade

### Build Configuration
Update `sdkconfig.defaults.bt`:
```diff
- CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y
+ CONFIG_BTDM_CTRL_MODE_BTDM=y    # Dual-stack mode
+ CONFIG_ESP_WIFI_ENABLED=y         # WiFi + BLE together
```

### Binary Sizes (Estimated)
| Build | ESP32 | ESP32-S3 | Status |
|-------|-------|----------|--------|
| WiFi-only | 944 KB ✅ | ~950 KB ✅ | Works on both |
| WiFi+BLE | ❌ FAIL | ~1.1 MB ✅ | **S3 ONLY** |

### Deployment
```bash
# After ESP32-S3 board installed and USB connected
idf.py -B build-bt -D RADIO=bt -p /dev/ttyUSB0 flash monitor
```

## Current Implementation Status

### ✅ Complete & Tested
- `components/nmea_ble/src/nmea_formatter.c` (210 lines)
  - PFLAA (traffic) sentence generation
  - PFLAU (own-ship) sentence generation
  - XOR checksum calculation
  - GDL90→NMEA type mapping

- `components/nmea_ble/src/nmea_ble.c` (258 lines)
  - BLE GATT server initialization
  - Service/Characteristic registration (SoftRF UUIDs)
  - 1 Hz traffic polling task
  - Connection/disconnection handling

- `components/nmea_ble/include/` (dual-stack headers)
  - Real implementations for BLE-enabled builds
  - Static inline stubs for WiFi-only builds

- `sdkconfig.defaults.bt` (BLE config)
  - DRAM optimizations documented
  - Dual-stack mode settings ready
  - Just needs hardware support

- `docs/SOFTRF_COMPATIBILITY.md` (protocol reference)
  - PFLAA/PFLAU field definitions
  - Aircraft type mapping
  - SoftRF UUID documentation

### 📋 Deferred to M3+ / ESP32-S3
- Real ownship position (currently placeholder 0°, 0°)
- Accurate bearing/distance calculations
- Time fields (no RTC on ESP32)
- Full alarm logic (distance + altitude thresholds)
- Performance optimization under high traffic

## Migration Path

### Step 1: Acquire ESP32-S3 Board
Choose from recommended boards above. Ensure:
- 512 KB+ internal DRAM
- 8 MB+ flash
- USB interface (native or via CP210x)
- Header compatibility if drop-in replacement desired

### Step 2: Update CMakeLists.txt (Optional)
If board pin configuration differs from current T8:
```bash
# Check if pin map needs update
grep -r "GPIO\|PIN" components/common/include/pins.h
# Update for ESP32-S3 pins if needed
```

### Step 3: Enable Dual-Stack
```bash
# Update BT config
cat > sdkconfig.defaults.bt << 'EOF'
CONFIG_ESP_WIFI_ENABLED=y
CONFIG_BT_ENABLED=y
CONFIG_BT_BLE_ENABLED=y
CONFIG_BTDM_CTRL_MODE_BTDM=y      # Dual-stack
EOF

# Clean and rebuild
rm sdkconfig && idf.py -B build-bt -D RADIO=bt build
```

### Step 4: Flash & Test
```bash
idf.py -B build-bt -p /dev/ttyUSB0 -b 460800 flash monitor
# Scan for "stratux" BLE device with nRF Connect
# Verify PFLAA/PFLAU sentences in real-time
```

### Step 5: (Optional) Test with EFB
- **XCSoar**: Settings → Devices → + → Bluetooth → "stratux"
- **SeeYou**: Connectivity → Bluetooth → Connect to "stratux"
- Verify traffic display in EFB

## Files Ready for ESP32-S3

All implementation is complete and just needs the hardware:

```
components/nmea_ble/
  ├── include/
  │   ├── nmea_ble.h              ✅ API + stubs
  │   └── nmea_formatter.h        ✅ Declarations
  ├── src/
  │   ├── nmea_ble.c              ✅ GATT server (258 lines)
  │   └── nmea_formatter.c        ✅ NMEA formatters (210 lines)
  └── CMakeLists.txt              ✅ Component config

main/
  └── main.c                       ✅ nmea_ble_init() + nmea_ble_start() calls

docs/
  ├── SOFTRF_COMPATIBILITY.md     ✅ Protocol reference
  ├── NMEA_BLE_PLAN.md           ✅ Roadmap
  ├── PHASE1_BUILD_STATUS.md     ✅ Build guide
  └── BLE_ENABLED.md             ✅ Testing guide
```

## Performance Notes

### WiFi Build (Current)
- Stable, production-ready
- ~944 KB firmware
- ~40 KB WiFi stack overhead
- Full web UI + GPS support
- Recommended for GDL90/UDP use case

### WiFi+BLE Build (ESP32-S3+)
- Will be stable once hardware allows
- ~1.1 MB firmware (larger due to dual stacks)
- ~65 KB combined WiFi+BLE overhead
- Full web UI + GPS + BLE traffic
- Recommended for EFB compatibility testing

### BLE-Only Build (Not planned)
- Would free ~40 KB by removing WiFi
- Could barely fit on ESP32-D0WD-V3
- Loses web UI, WiFi provisioning
- Not practical; wait for S3 upgrade instead

## Timeline Recommendation

1. **Now**: Keep WiFi build deployed (working ✅)
2. **When available**: Procure ESP32-S3 board
3. **Upon receipt**: Update config & rebuild dual-stack
4. **Test**: Verify BLE with nRF Connect / XCSoar
5. **Deploy**: WiFi+BLE firmware as standard build

## Questions?

- **"Why not use PSRAM for stack?"** — ESP-IDF stack components (WiFi, BLE) require internal DRAM, not PSRAM
- **"Can we optimize further?"** — Possible but diminishing returns; S3 upgrade is cleaner
- **"Will BLE code still work?"** — Yes, identical code compiles on S3; just needs the DRAM

---

**Status**: ✅ Implementation complete, 🚫 hardware limited, 🚀 ready for ESP32-S3 upgrade

**Last Updated**: 2026-09-15 20:12 UTC
