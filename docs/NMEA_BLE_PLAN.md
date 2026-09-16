# NMEA over BLE Implementation Plan for stratux-esp32

## Overview
Add Bluetooth Low Energy (BLE) support to transmit traffic data as NMEA sentences, inspired by:
- **Stratux**: FLARM/NMEA sentence generation (`main/flarm-nmea.go`)
- **Moshe Braner's SoftRF**: BLE GATT implementation patterns (TinyGo bluetooth)

## Architecture

### Component Structure
```
components/nmea_ble/
├── include/
│   ├── nmea_ble.h           // Public API
│   └── nmea_formatter.h     // NMEA sentence generation
├── src/
│   ├── nmea_ble.c           // BLE GATT server + task
│   └── nmea_formatter.c     // PFLAA/PFLAU/NMEA sentence builders
└── CMakeLists.txt
```

### Key Files to Create

#### 1. **components/nmea_ble/include/nmea_formatter.h**
- `nmea_format_pflaa()` — Format FLARM traffic alert (RelativeNorth/East/Vertical)
- `nmea_format_pflau()` — Format FLARM own ship status (RX/TX/GPS/Power/Alarm)
- `nmea_checksum()` — Calculate XOR checksum
- Helper functions for coordinate conversion (LatLng to relative North/East)

#### 2. **components/nmea_ble/include/nmea_ble.h**
- `nmea_ble_init()` — Initialize BLE adapter + GATT service
- `nmea_ble_start()` — Start BLE task (configurable via settings)
- `nmea_ble_is_connected()` — Check if client connected
- Predefined UUIDs:
  - **SoftRF profile**: Service `0xFFE0`, Characteristic `0xFFE1`
  - **Nordic UART**: Service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`, RX/TX chars

#### 3. **components/nmea_ble/src/nmea_formatter.c**
Port key logic from Stratux's `flarm-nmea.go`:
- PFLAA sentence: `$PFLAA,<AlarmLevel>,<RelN>,<RelE>,<RelV>,<IDType>,<ID>,<Track>,<TurnRate>,<GS>,<ClimbRate>,<AcftType>*xx`
- PFLAU sentence: `$PFLAU,<RX>,<TX>,<GPS>,<Power>,<AlarmLevel>,<RelBearing>,<AlarmType>,<RelV>,<RelDist>,<ID>*xx`
- Alarm level computation (distance + vertical thresholds)
- GDL90 emitter category → NMEA aircraft type mapping

#### 4. **components/nmea_ble/src/nmea_ble.c**
- **BLE init**: `esp_ble_gap_*` + `esp_ble_gatts_*` APIs
- **GATT characteristic**: Notifications (push NMEA lines to clients)
- **Task**: `nmea_ble_task()` polls `traffic_snapshot()` @ 1 Hz, formats sentences, sends notifications
- **Coordinate conversion**: Needs ownship position (M3 GPS integration)
  - Until M3: Use placeholder ownship at `(0, 0)` or make it configurable

### Integration Points

#### With Traffic Table
```c
traffic_info_t traffic_snapshot[TRAFFIC_TABLE_MAX];
size_t count = traffic_snapshot(traffic_snapshot, TRAFFIC_TABLE_MAX);
```

#### With Settings (Optional, M2+)
- Add to `settings_t`:
  ```c
  bool ble_en;           // NVS "ble_en", default false
  uint16_t ble_update_hz; // Update frequency, default 1 Hz
  ```

#### With Ownship Position (M3)
- Placeholder: Hard-code or make configurable for now
- M3 dependency: GPS provides lat/lng for relative position calcs

### NMEA Sentence Format Reference

**PFLAA (Traffic Alert):**
```
$PFLAA,<AlarmLevel>,<RelNorth>,<RelEast>,<RelVertical>,<IDType>,<ID>,<Track>,<TurnRate>,<GroundSpeed>,<ClimbRate>,<AcftType>*<checksum>\r\n
```
- `AlarmLevel`: 0 (no alarm), 1–3 (escalating)
- `RelNorth/East/Vertical`: Meters, signed
- `IDType`: 1 (ICAO), 2 (FLARM ID), 3 (anon)
- `ID`: 6-digit hex ICAO address (+ optional `!CALLSIGN`)
- `AcftType`: 0–F hex (8=piston, 9=jet, 3=heli, 1=glider)

**PFLAU (Own Ship):**
```
$PFLAU,<RX>,<TX>,<GPS>,<Power>,<AlarmLevel>,<RelBearing>,<AlarmType>,<RelVertical>,<RelDistance>,<ID>*<checksum>\r\n
```
- `RX`: Active RX count (# of traffic)
- `TX`: TX status (1=sending)
- `GPS`: 0 (no fix), 2 (3D fix)
- `Power`: 1 (OK)
- `AlarmLevel`: 0–3

### Testing Strategy

1. **Unit**: NMEA sentence format (checksum, field order)
2. **Integration**: Traffic → NMEA output in logs (no BLE device needed)
3. **On-device**: 
   - Connect BLE phone app (e.g., nRF Connect)
   - Verify GATT service advertised
   - Observe NMEA notifications streamed
   - Validate with SoftRF-style or Nordic UART profile

### Implementation Phasing

**Phase 1 (Core):**
- NMEA formatter (PFLAA/PFLAU generation, checksums)
- BLE GATT service (fixed UUID, single characteristic)
- Task loop (traffic → NMEA → notify)
- Placeholder ownship position

**Phase 2 (Polish):**
- Settings (ble_en, update rate)
- Multiple profiles (SoftRF + Nordic UART)
- Web UI: BLE status, client count
- Alarm level logic (distance + vertical thresholds)

**Phase 3 (M3 Integration):**
- Ownship position from GPS (M3)
- Heading from GPS
- Altitude from GPS/baro (for vertical calculations)

### Gotchas
- **No RTC**: Until GPS (M3), use current FLARM convention (placeholder values)
- **PSRAM split**: NMEA buffers (~256 bytes) fit in heap; not a concern
- **BLE stack**: TinyGo `tinygo.org/x/bluetooth` vs. NimBLE (check ESP-IDF docs)
- **Coordinate math**: Stratux uses `common.DistRect()` (great-circle distance); for now, use Pythagorean approximation or omit until M3

## References
- Stratux `main/flarm-nmea.go`: FLARM sentence encoding, alarm logic
- SoftRF network.go: BLE GATT patterns (adapted for TinyGo/C)
- FLARM Protocol Spec: PFLAA/PFLAU format details
- AGENTS.md: Task architecture, ownship status (M3 GPS)
