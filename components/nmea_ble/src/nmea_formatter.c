#include "sdkconfig.h"

#ifdef CONFIG_BT_BLE_ENABLED

#include "nmea_formatter.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// NMEA checksum: XOR of all bytes between $ and *
static uint8_t nmea_checksum(const char *sentence) {
    uint8_t sum = 0;
    const char *p = sentence;
    
    // Skip $
    if (*p == '$') p++;
    
    // XOR until * or end
    while (*p && *p != '*') {
        sum ^= (uint8_t)*p;
        p++;
    }
    return sum;
}

// GDL90 emitter category → NMEA aircraft type (hex)
// See SOFTRF_COMPATIBILITY.md for mapping table
uint8_t gdl90_emitter_to_nmea_acft_type(uint8_t gdl90_emitter_cat) {
    switch (gdl90_emitter_cat) {
        case 1:
        case 6:
            return 0x8;  // piston
        case 2:
        case 3:
        case 4:
        case 5:
            return 0x9;  // jet
        case 7:
            return 0x3;  // helicopter
        case 9:
            return 0x1;  // glider
        case 10:
            return 0xB;  // balloon
        case 11:
            return 0x4;  // skydiver
        case 12:
            return 0x7;  // paraglider
        case 14:
            return 0xD;  // UAV
        case 17:
        case 18:
            return 0xE;  // ground support / surface vehicle
        case 19:
            return 0xF;  // static object
        default:
            return 0x0;  // unknown
    }
}

// Absolute to relative coordinate conversion (simplified, non-M3).
// For MVP: uses Pythagorean approximation without great-circle math.
// Earth radius ≈ 6371 km; 1 degree ≈ 111 km at equator.
static void compute_relative_coords(double traffic_lat, double traffic_lng,
                                     double ownship_lat, double ownship_lng,
                                     int32_t *out_rel_north, int32_t *out_rel_east) {
    const double lat_per_meter = 111000.0;  // 111 km/degree
    const double cos_lat = cos((traffic_lat + ownship_lat) / 2.0 * M_PI / 180.0);
    const double lng_per_meter = 111000.0 * cos_lat;
    
    double dlat = traffic_lat - ownship_lat;
    double dlng = traffic_lng - ownship_lng;
    
    *out_rel_north = (int32_t)(dlat * lat_per_meter);
    *out_rel_east = (int32_t)(dlng * lng_per_meter);
}

// Data source code (from MB fork NMEA.cpp)
static uint8_t traffic_src_to_data_source(uint8_t src) {
    if (src & TRAFFIC_SRC_ES) return 1;   // ADS-B (1090ES)
    if (src & TRAFFIC_SRC_UAT) return 4;  // TIS-B (978 UAT)
    return 0;
}

size_t nmea_format_pflaa(const traffic_info_t *traffic,
                         double ownship_lat, double ownship_lng,
                         char *buf, size_t len) {
    if (!buf || !len || !traffic || !traffic->position_valid) {
        return 0;
    }
    
    // Compute relative coordinates (North/East in meters)
    int32_t rel_north, rel_east;
    compute_relative_coords(traffic->lat, traffic->lng, ownship_lat, ownship_lng,
                            &rel_north, &rel_east);
    
    // Altitude difference (feet to meters: * 0.3048)
    int32_t rel_vertical = (int32_t)((traffic->alt_ft - 0) * 0.3048);
    
    // Address type (1=ICAO, 2=FLARM ID, 3=anonymous)
    uint8_t addr_type = 1;  // Always ICAO for ADS-B/UAT
    
    // Aircraft type (GDL90 → NMEA)
    uint8_t acft_type = 0;
    if (traffic->cat_valid) {
        acft_type = gdl90_emitter_to_nmea_acft_type(traffic->emitter_cat);
    }
    
    // Alarm level (0=no alarm; MVP doesn't compute alarms yet)
    uint8_t alarm_level = 0;
    
    // Data source code
    uint8_t data_source = traffic_src_to_data_source(traffic->src_bands);
    
    // Signal strength (already raw hex from Pong, from traffic.ss)
    uint16_t rssi = traffic->ss;
    
    // Format sentence: $PFLAA,alarm,relN,relE,relV,addrType,ICAO!callsign,course,speed,climbRate,acftType,noTrack,dataSource,rssi*checksum\r\n
    size_t pos = 0;
    
    if (traffic->tail_valid && traffic->tail[0]) {
        // With callsign: ICAO!callsign_ICAO
        pos = snprintf(buf, len,
                       "$PFLAA,%d,%d,%d,%d,%d,%06X!%s_%06X,%d,,%d,%X,%d,%d,%d",
                       (int)alarm_level, (int)rel_north, (int)rel_east, (int)rel_vertical,
                       addr_type, (unsigned int)traffic->icao_addr, traffic->tail, (unsigned int)traffic->icao_addr,
                       traffic->track_deg, traffic->speed_kt, acft_type,
                       0, data_source, rssi);
    } else {
        // Without callsign: just ICAO
        pos = snprintf(buf, len,
                       "$PFLAA,%d,%d,%d,%d,%d,%06X,%d,,%d,%X,%d,%d,%d",
                       (int)alarm_level, (int)rel_north, (int)rel_east, (int)rel_vertical,
                       addr_type, (unsigned int)traffic->icao_addr,
                       traffic->track_deg, traffic->speed_kt, acft_type,
                       0, data_source, rssi);
    }
    
    if (pos + 6 >= len) return 0;  // Buffer too small
    
    // Append checksum
    uint8_t checksum = nmea_checksum(buf);
    pos += snprintf(buf + pos, len - pos, "*%02X\r\n", checksum);
    
    return pos;
}

size_t nmea_format_pflau(size_t traffic_count, const traffic_info_t *hp_traffic,
                         bool gps_valid,
                         char *buf, size_t len) {
    if (!buf || !len) {
        return 0;
    }
    
    // TX status (1=transmitting, always on for Stratux)
    uint8_t tx_status = 1;
    
    // GPS status (0=no fix, 2=3D fix)
    uint8_t gps_status = gps_valid ? 2 : 0;
    
    // Power status (1=OK, always for ESP32)
    uint8_t power_status = 1;
    
    // Alarm level of highest priority traffic (0 for MVP)
    uint8_t alarm_level = 0;
    
    // Relative bearing to HP target (empty field for non-directional or no HP)
    char rel_bearing_str[8] = "";
    if (hp_traffic && hp_traffic->position_valid) {
        // TODO: M3 GPS — compute bearing from ownship track
        // For MVP: leave empty
    }
    
    // Alarm type (0=no alarm, 2=aircraft)
    uint8_t alarm_type = alarm_level > 0 ? 2 : 0;
    
    // HP target altitude difference (placeholder 0)
    int32_t hp_alt_diff = 0;
    
    // HP target distance (placeholder 999 meters)
    uint32_t hp_distance = 999;
    
    // HP target ICAO address (or dummy)
    uint32_t hp_addr = hp_traffic ? hp_traffic->icao_addr : 0x000000;
    
    size_t pos = 0;
    
    if (traffic_count > 0 && hp_traffic) {
        // With traffic
        pos = snprintf(buf, len,
                       "$PFLAU,%zu,%d,%d,%d,%d,%s,%d,%d,%u,%06X",
                       traffic_count, tx_status, gps_status, power_status, alarm_level,
                       rel_bearing_str, alarm_type, (int)hp_alt_diff, (unsigned int)hp_distance, (unsigned int)hp_addr);
    } else {
        // No traffic
        pos = snprintf(buf, len,
                       "$PFLAU,%zu,%d,%d,%d,%d,,0,,,",
                       traffic_count, tx_status, gps_status, power_status, alarm_level);
    }
    
    if (pos + 6 >= len) return 0;  // Buffer too small
    
    // Append checksum
    uint8_t checksum = nmea_checksum(buf);
    pos += snprintf(buf + pos, len - pos, "*%02X\r\n", checksum);
    
    return pos;
}

#endif  // CONFIG_BT_BLE_ENABLED
