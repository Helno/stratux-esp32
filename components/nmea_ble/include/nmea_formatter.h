#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef CONFIG_BT_BLE_ENABLED

#include "traffic.h"

// NMEA FLARM sentence formatters (PFLAA/PFLAU).
// Ported from Stratux main/flarm-nmea.go with SoftRF/Moshe Braner compatibility.
// See SOFTRF_COMPATIBILITY.md for field details.

// Format a traffic entry as NMEA PFLAA (FLARM traffic alert) sentence.
// Buf must be at least 256 bytes. Returns length including checksum and CRLF.
// ownship_lat/lng: reference position for relative coordinate calculation (degrees).
// Until M3 GPS: use placeholder (0.0, 0.0) — relative positions will be absolute.
size_t nmea_format_pflaa(const traffic_info_t *traffic,
                         double ownship_lat, double ownship_lng,
                         char *buf, size_t len);

// Format own-ship status as NMEA PFLAU (FLARM status) sentence.
// Buf must be at least 256 bytes. Returns length including checksum and CRLF.
// traffic_count: current number of tracked aircraft in table.
// hp_traffic: highest-priority target (or NULL if none).
// gps_valid: true if ownship position is valid (GPS M3+).
size_t nmea_format_pflau(size_t traffic_count, const traffic_info_t *hp_traffic,
                         bool gps_valid,
                         char *buf, size_t len);

// GDL90 emitter category to NMEA aircraft type (hex).
// Maps GDL90 emitter categories (0–39) to FLARM aircraft types (0–F).
uint8_t gdl90_emitter_to_nmea_acft_type(uint8_t gdl90_emitter_cat);

#endif  // CONFIG_BT_BLE_ENABLED
