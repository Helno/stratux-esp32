#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef CONFIG_BT_BLE_ENABLED

#include "traffic.h"
#include "gps.h"

// NMEA FLARM sentence formatters (PFLAA/PFLAU).
// Ported from Stratux main/flarm-nmea.go with SoftRF/Moshe Braner compatibility.
// See SOFTRF_COMPATIBILITY.md for field details.

// Format a traffic entry as NMEA PFLAA (FLARM traffic alert) sentence.
// Buf must be at least 256 bytes. Returns length including checksum and CRLF.
// Positions and altitude are relative to the ownship GPS fix; returns 0 without one.
size_t nmea_format_pflaa(const traffic_info_t *traffic,
                         const gps_ownship_t *own,
                         char *buf, size_t len);

// Format own-ship status as NMEA PFLAU (FLARM status) sentence.
// Buf must be at least 256 bytes. Returns length including checksum and CRLF.
// traffic_count: current number of tracked aircraft in table.
// targets/n_targets: candidates; the nearest one with a position is reported
// as the highest-priority target (bearing, altitude difference, distance).
size_t nmea_format_pflau(size_t traffic_count, const traffic_info_t *targets, size_t n_targets,
                         const gps_ownship_t *own,
                         char *buf, size_t len);

// Format ownship position as $GPRMC / $GPGGA (UTC from the GPS-synced system
// clock). Returns 0 if there is no valid fix or the clock isn't synced yet.
// Required by SkyView, which reports "GPS NO FIX" without both sentences.
size_t nmea_format_gprmc(const gps_ownship_t *own, char *buf, size_t len);
size_t nmea_format_gpgga(const gps_ownship_t *own, char *buf, size_t len);

// GDL90 emitter category to NMEA aircraft type (hex).
// Maps GDL90 emitter categories (0–39) to FLARM aircraft types (0–F).
uint8_t gdl90_emitter_to_nmea_acft_type(uint8_t gdl90_emitter_cat);

#endif  // CONFIG_BT_BLE_ENABLED
