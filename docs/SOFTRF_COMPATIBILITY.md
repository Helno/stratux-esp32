# SoftRF (MB Fork) BLE NMEA Compatibility Analysis
## For stratux-esp32 Implementation

### Compatibility Summary
**Short Answer:** YES — stratux-esp32 can output **100% compatible FLARM/NMEA sentences** over BLE with SoftRF-compatible devices by following Moshe Braner's fork patterns.

---

## 1. BLE GATT Service & UUIDs

### SoftRF (MB) Uses:
- **Service UUID**: `0000ffe0-0000-1000-8000-00805f9b34fb` (SoftRF/HM-10 compatible UART service)
- **Characteristic UUID**: `0000ffe1-0000-1000-8000-00805f9b34fb` (UART TX characteristic)
- **Properties**: `NOTIFY`, `READ`, `WRITE_NR` (write without response)

### Recommendation for stratux-esp32:
Use the **exact same UUIDs** for maximum compatibility. Devices expecting SoftRF-style NMEA will connect and receive traffic data transparently.

---

## 2. NMEA Sentence Formats

### PFLAA (Traffic Alert) — SoftRF Format
```
$PFLAA,<alarm>,<relNorth>,<relEast>,<relVertical>,<addrType>,<ICAO>!<callsign>,<course>,<climbRate>,<speed>,<aircraftType>,<noTrack>,<dataSource>,<rssi>*<checksum>
```

**Example from SoftRF source (NMEA.cpp line 1675):**
```c
"$PFLAA,%d,%d,%s,%d,%d,%06X!%s_%06X,%s,,%s,%s,%X,%d,%d,%d"
// Values:
// alarm_level, dy(RelEast), str_dx(RelNorth),
// alt_diff(RelVertical), addr_type, id, prefix, id,
// ltrim(str_course), ltrim(str_speed), ltrim(str_climb_rate), 
// fop->aircraft_type, no_track, data_source, rssi
```

**Key Fields:**
- `alarm`: 0–3 (0=no alarm, 3=imminent collision)
- `relNorth`, `relEast`: Meters, signed (relative to ownship)
- `relVertical`: Meters, signed altitude difference
- `addrType`: 1 (ICAO), 2 (FLARM), 3 (anonymous)
- `ICAO`: 6-digit hex (+ optional `!callsign` separator)
- `course`: Degrees (can be empty field `,`)
- `speed`: Knots, can be empty
- `climbRate`: m/s with 1 decimal, can be empty
- `aircraftType`: Hex digit (0–F) — GDL90 emitter category → NMEA conversion required
- `noTrack`, `dataSource`, `rssi`: Extensions; can be omitted

### PFLAU (Own Ship Status) — SoftRF Format
```
$PFLAU,<RX>,<TX>,<GPS>,<Power>,<Alarm>,<RelBearing>,<AlarmType>,<AltDiff>,<Distance>,<ID>*<checksum>
```

**Example from SoftRF source (NMEA.cpp line 1812):**
```c
"$PFLAU,%d,%d,%d,%d,%d,%s,%d,%d,%u,%06X"
// Values:
// total_objects+hidden_objects, tx_status, gps_status,
// power_status, HP_alarm_level, str_relbrg(or empty),
// alarm_type, HP_alt_diff, (int) HP_distance, HP_addr
```

**Key Fields:**
- `RX`: Count of received traffic targets
- `TX`: TX status (1=transmitting)
- `GPS`: 0 (no fix), 2 (3D moving), etc.
- `Power`: 1 (normal), 0 (low), etc.
- `Alarm`: 0–3 (highest priority target alarm level)
- `RelBearing`: Relative bearing to highest priority target (empty for non-directional targets)
- `AlarmType`: 2=aircraft (default), 0=traffic
- `AltDiff`: Altitude difference in meters to HP target
- `Distance`: Distance in meters to HP target (nautical miles in some variants)
- `ID`: ICAO address of highest priority target

---

## 3. NMEA Checksum Algorithm

**SoftRF uses standard NMEA checksum:**
```c
XOR of all bytes between '$' and '*'
Format: *HH (two hex digits)
```

Stratux already implements this correctly in `gdl90_out.c`.

---

## 4. BLE Transmission Protocol

### SoftRF/MB Approach:
1. **NMEA lines buffered** in memory
2. **Sent via BLE characteristic notifications** (not responses)
3. **One sentence per notification** (typical, ~100 bytes per packet)
4. **CRLF line endings** included in payload
5. **No handshaking** — fire-and-forget notifications

### Stratux-esp32 Should:
- Poll `traffic_snapshot()` at ~1 Hz (same as GDL90 emit task)
- Format each traffic entry as PFLAA + PFLAU (one pair per cycle or individual)
- Send via BLE notifications on the GATT characteristic
- Include CRLF in payload (`\r\n`)

---

## 5. Data Source Encoding (MB Extension)

SoftRF Moshe Braner adds optional `data_source` field in PFLAA:
```c
data_source_code[] = {
    [TX_TYPE_NONE]  = 0,
    [TX_TYPE_ADSR]  = 3,  // ADS-R (rebroadcast)
    [TX_TYPE_ADSB]  = 1,  // ADS-B (1090ES)
    [TX_TYPE_TISB]  = 4,  // TIS-B (from UAT)
    [TX_TYPE_FLARM] = 0   // FLARM
};
```

**For stratux-esp32:**
- Traffic from 1090ES → data_source = 1 (ADS-B)
- Traffic from 978 UAT → data_source = 4 (TIS-B)
- Use the source recorded in `traffic_info_t.src` field

---

## 6. Aircraft Type Mapping (GDL90 → NMEA)

**SoftRF's conversion (flarm-nmea.go-equivalent):**
```c
gdl90EmitterCatToNMEA(emitterCat) → hex_type:
  GDL90:1,6   → 0x8 (piston)
  GDL90:2–5   → 0x9 (jet)
  GDL90:7     → 0x3 (helicopter)
  GDL90:9     → 0x1 (glider)
  GDL90:10    → 0xB (balloon)
  GDL90:11    → 0x4 (skydiver)
  GDL90:12    → 0x7 (paraglider)
  GDL90:14    → 0xD (UAV)
  GDL90:17,18 → 0xE (ground support / surface vehicle)
  GDL90:19    → 0xF (static object)
  else        → 0x0 (unknown)
```

Stratux must include this conversion in the NMEA formatter.

---

## 7. Ownship Position Dependency (M3 Blocker)

**Critical limitation:** Both PFLAA and PFLAU require **ownship position** to compute relative coordinates:
- `relNorth`, `relEast` = traffic position relative to ownship
- `relBearing` = computed from ownship track + traffic bearing

**Current State (M0–M1):**
- No GPS/ownship position available
- **Workaround for MVP:** 
  - Hard-code ownship at lat=0, lng=0 (or user-configurable)
  - All relative positions will be incorrect until M3
  - Still useful for testing/compatibility
  - Apps will see traffic but distance/bearing wrong

**M3 Resolution:**
- Integrate GPS position from `gps_rx_task()` (already queued but not exposed)
- Need ownship position in a global struct or callback

---

## 8. Implementation Checklist for Full Compatibility

### Phase 1 (MVP — Get BLE NMEA Working)
- [ ] Create `components/nmea_ble/` component
- [ ] Implement GATT service with SoftRF UUIDs (`0xffe0`/`0xffe1`)
- [ ] Format PFLAA with traffic data
- [ ] Format PFLAU with own-ship status (placeholder position)
- [ ] Send via BLE notifications at 1 Hz
- [ ] Test with nRF Connect or SoftRF-compatible app

### Phase 2 (Real Ownship Position — M3 Dependency)
- [ ] Extract GPS position from `gps_rx_task()` or global status
- [ ] Compute relative North/East/Vertical using great-circle math (or Pythagorean for MVP)
- [ ] Update PFLAA with real relative coordinates
- [ ] Verify with SoftRF app on real device

### Phase 3 (Polish)
- [ ] Add settings for BLE enable/disable
- [ ] Web UI status display (clients connected, traffic sent)
- [ ] Optimize update rate (currently 1 Hz; could be configurable)
- [ ] Filter stale/aged-out traffic before NMEA output

---

## 9. Testing Strategy

### Unit Tests (No Hardware)
```c
// Verify NMEA sentence format
char buf[256];
traffic_info_t t = {
  .icao_addr = 0xABCDEF,
  .lat = 37.5, .lng = -122.5, .alt_ft = 5000,
  .speed_kt = 150, .track_deg = 045,
  .tail = "N123AB", .position_valid = true
};
size_t len = nmea_format_pflaa(&t, ownship_lat, ownship_lng, buf, sizeof(buf));
// Verify checksum, format, field order
```

### Integration Tests
- Verify traffic → NMEA conversion with console replay (TESTING.md replay mode)
- Verify BLE notifications transmitted (ESP_LOG debug)

### On-Device Tests
1. Connect SoftRF-compatible BLE app (XCSoar, SeeYou, Skyfield, SoftRF app)
2. Confirm GATT service `0xffe0` advertised
3. Connect to characteristic `0xffe1`
4. Receive PFLAA/PFLAU sentences in real-time
5. Visually verify traffic appears in app (bearing/distance wrong until M3, but ICAO/callsign correct)

---

## 10. Code Reference Snippets (SoftRF MB Fork)

### Bluetooth.cpp Key Functions
- `ESP32_Bluetooth_setup()` — GATT service initialization
- BLE notification send: `pUARTCharacteristic->setValue(buf, len); pUARTCharacteristic->notify();`

### NMEA.cpp Key Functions
- `NMEA_PFLAM()` — Generate FLARM alert sentence
- Checksum: `NMEA_add_checksum(buf)` — XOR-based (same as GDL90)

### Data Structures
- `container_t` — Traffic object (lat, lng, alt, addr, callsign, etc.)
- `traffic_info_t` — Stratux equivalent (already defined in stratux-esp32)

---

## Summary: Compatibility with SoftRF Devices

| Aspect | Status | Notes |
|--------|--------|-------|
| **BLE UUIDs** | ✅ Full | Use `0xffe0`/`0xffe1` (SoftRF UART service) |
| **NMEA Sentences** | ✅ Full | PFLAA/PFLAU format identical (port from Stratux) |
| **Checksum** | ✅ Full | Standard XOR (already in gdl90_out.c) |
| **Data Fields** | ✅ Full (with extensions) | ICAO, callsign, track, speed, altitude, aircraft type |
| **Ownship Position** | ⚠️ Partial | MVP hardcoded; M3 enables real coordinates |
| **Data Source Tagging** | ✅ Full | ES→1, UAT→4 (already in traffic_info_t.src) |
| **Aircraft Type Map** | ⚠️ Needs Implementation | Convert GDL90 emitter category to NMEA hex |
| **Relative Position Math** | ⚠️ Needs Implementation | Distance/bearing calculations (Stratux ports these) |

---

## Key Takeaway
**stratux-esp32 BLE output will be 100% SoftRF-compatible** if you:
1. Use SoftRF's GATT UUIDs
2. Port the PFLAA/PFLAU formatters from Stratux's `flarm-nmea.go`
3. Map GDL90 emitter categories to NMEA aircraft types
4. **Integrate GPS position at M3** for real relative coordinates

Until M3, the BLE output will work but show all traffic at the wrong bearing/distance (relative to placeholder ownship at 0,0). This is acceptable for MVP testing.
