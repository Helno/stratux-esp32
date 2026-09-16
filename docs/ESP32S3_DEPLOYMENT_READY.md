# ESP32-S3 Deployment - Ready for Testing

**Status**: ✅ BLE firmware successfully compiled and ready to flash
**Binary size**: 0xEBCD0 bytes (~955 KB) - plenty of headroom in 4 MB flash
**Build directory**: `build-bt/` (contains bootloader, partition table, and firmware)

## Quick Start (When S3 Arrives)

### Step 1: Erase and Flash
```bash
cd ~/stratux-esp32
source ~/esp-idf-5.5.2/export.sh

# Erase old flash
idf.py -B build-bt -D RADIO=bt -p /dev/ttyUSB0 erase-flash

# Flash and monitor
idf.py -B build-bt -D RADIO=bt -p /dev/ttyUSB0 flash monitor
```

**Expected output:**
- Boot messages with chipset info
- "WiFi initialized"
- "HTTP server started on 192.168.1.x"
- "BLE advertising Stratux service" ← **This is the new part!**

### Step 2: Verify WiFi Works
```bash
# Find the IP from serial output
# Open browser to: http://192.168.1.xxx
# Should see traffic map and config page (same as D0WD)
```

### Step 3: Verify BLE Advertising (NEW!)
```bash
# On Linux computer:
hciconfig              # Verify Bluetooth is available
hcitool scan           # Scan for BLE devices

# Expected output:
#   Scanning ...
#   60:55:f9:xx:xx:xx   Stratux  ← This is your device!
```

### Step 4: Test NMEA Sentences via BLE (Advanced)
```bash
# Using gatttool (Linux):
gatttool -b 60:55:f9:xx:xx:xx interactive

# Once connected:
> connect
> characteristics
> select-attribute 0000ffe1-0000-1000-8000-00805f9b34fb
> notify on

# Should see NMEA sentences streaming in:
# $PFLAA,1,FF,0103,0000,DEADBEEF,0010,0000*2C
# $PFLAA,2,FF,0104,0000,DECAFBAD,0020,0000*3D
```

## Hardware Validation Checklist

### Checklist Item 1: CPU and Memory
```bash
# From serial monitor output, you should see:
# - "CPU frequency: 240 MHz"
# - "PSRAM: 8 MB" ← Confirms 8 MB external RAM
# - "Free heap: xxx bytes" (should be >150 KB)
```

### Checklist Item 2: DRAM Status
- Dual WiFi + BLE stack should work with NO OVERFLOW
- Previous D0WD would exceed 128 KB DRAM
- S3 has 512 KB DRAM → plenty of headroom

### Checklist Item 3: Simultaneous WiFi + BLE
- Open browser to HTTP while BLE scan is running
- Both should work at the same time
- No crashes or watchdog resets
- This was impossible on D0WD

## File Locations

- **Current firmware**: `build-bt/stratux_esp32.bin` (955 KB)
- **BLE component**: `components/nmea_ble/src/nmea_ble.c` (258 lines)
- **NMEA formatter**: `components/nmea_ble/src/nmea_formatter.c` (210 lines)
- **Public API**: `components/nmea_ble/include/nmea_ble.h`
- **Protocol docs**: `docs/SOFTRF_COMPATIBILITY.md`
- **Hardware limits**: `docs/HARDWARE_LIMITATION_ESP32_S3_UPGRADE.md`

## Testing with EFB (Optional)

If you have XCSoar or SeeYou on a phone/tablet:

### WiFi UDP Method
1. Connect phone to "Stratux" WiFi
2. Open XCSoar → Settings → Device
3. Add device: "UDP Port 4000"
4. Enable
5. Should see traffic arrows on map

### BLE Method (Better!)
1. Pair phone to "Stratux" via Bluetooth settings
2. Open XCSoar → Settings → Device
3. Add device: "Stratux" (should auto-discover BLE)
4. Enable
5. Should see traffic arrows (faster than WiFi)

## Next Steps (Phase 2: AHRS)

After confirming BLE works:

1. **Check for IMU sensor**:
   ```bash
   sudo i2cdetect -y 1
   # Look for device at address 0x68 (MPU6050)
   # If present: ✅ No soldering needed
   # If missing: Add external MPU6050 module (~$5-8)
   ```

2. **Create AHRS component skeleton**:
   ```bash
   mkdir -p components/ahrs/src
   mkdir -p components/ahrs/include
   # Create ahrs.h with function signatures
   # Create ahrs.c with implementation
   ```

3. **Implement Kalman filter integration**:
   - Read IMU at 100 Hz (10 ms loop)
   - Fuse accelerometer + gyroscope + magnetometer
   - Update Euler angles (pitch, roll, yaw)
   - Output via HTTP + BLE

4. **Test with EFB**:
   - XCSoar should display artificial horizon
   - Real-time pitch/roll visualization

## Troubleshooting

### BLE doesn't advertise
- Check serial monitor for "BLE advertising" message
- Verify `CONFIG_BT_BLE_ENABLED=y` in compiled config
- Try restarting with `idf.py -B build-bt -p /dev/ttyUSB0 flash monitor`

### WiFi connects but BLE doesn't work
- This is normal on D0WD (hardware limit)
- On S3, both should work together
- If not, check DRAM usage in serial output

### NMEA sentences not appearing
- Verify traffic table has aircraft
- Check that notifications are enabled on BLE characteristic
- Look for error messages in serial monitor

### Can't connect to browser at 192.168.1.x
- Find IP address from serial monitor output
- Check WiFi is enabled (web UI should have toggle)
- Verify your computer is on same WiFi network

## Success Criteria

✅ All of these should work on ESP32-S3:
- [ ] Serial monitor shows boot messages (no errors)
- [ ] HTTP server responds to browser requests
- [ ] BLE device appears in `hcitool scan`
- [ ] NMEA sentences appear when connected via BLE
- [ ] Simultaneous WiFi + BLE (no interference)
- [ ] Free heap stays stable (no memory leak)
- [ ] No watchdog resets or crashes over 1 hour

## Commit to Version Control

```bash
# Before flashing S3:
git add -A
git commit -m "Pre-S3 deployment: WiFi + BLE + NMEA ready for testing"

# After S3 boots successfully:
git tag -a v0.3-s3-ready -m "First successful ESP32-S3 deployment with dual-stack"
git push origin main --tags
```

This marks the milestone where you've validated:
- ✅ Code compiles for S3
- ✅ BLE dual-stack works on S3  
- ✅ NMEA sentences transmit correctly
- ✅ Ready to proceed with Phase 2 (AHRS)

## Technical Details

### Memory Usage Breakdown (S3)

| Component | Memory | Status |
|-----------|--------|--------|
| Internal DRAM | 512 KB | ✅ Has 4× more than D0WD |
| WiFi stack | ~40 KB | ✅ Fits easily |
| BLE stack | ~25 KB | ✅ Fits easily (was blocker on D0WD) |
| Traffic table (300 aircraft) | ~36 KB | ✅ Fits easily |
| NMEA/BLE code | ~50 KB | ✅ Fits easily |
| HTTP server | ~20 KB | ✅ Fits easily |
| FreeRTOS + tasks | ~15 KB | ✅ Fits easily |
| **Total used** | **~186 KB** | **✅ Well within 512 KB** |
| **Free headroom** | **~326 KB** | **✅ Plenty for AHRS + audio** |

### Protocol Compliance

- Service UUID: `0000ffe0-0000-1000-8000-00805f9b34fb` (HM-10 standard)
- Characteristic UUID: `0000ffe1-0000-1000-8000-00805f9b34fb` (HM-10 standard)
- NMEA format: `$PFLAA,fields...*checksum\r\n`
- Update rate: 1 Hz (matches GDL90 emit task)
- Compatible with: XCSoar, SeeYou, other SoftRF-aware EFBs

### Build System Notes

- **Build directory**: `build-bt/` (for BLE) vs `build-wifi/` (for WiFi-only)
- **Config file**: `sdkconfig.defaults.bt` enables `CONFIG_BT_BLE_ENABLED=y`
- **CMake variable**: `-D RADIO=bt` tells build system which config to use
- **No code changes needed**: Same codebase, just different compile flags

## Need Help?

Check these docs for detailed info:
- `docs/SOFTRF_COMPATIBILITY.md` - Protocol details
- `docs/NMEA_BLE_PLAN.md` - Implementation roadmap (Phase 1-3)
- `docs/HARDWARE_LIMITATION_ESP32_S3_UPGRADE.md` - Why S3 was needed
- `docs/PHASE1_BUILD_STATUS.md` - Configuration guide

Good luck! The BLE functionality you've been waiting for is now ready to test. 🚀
