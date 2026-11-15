#include "sdkconfig.h"

#ifdef CONFIG_BT_BLE_ENABLED

#include "nmea_formatter.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

static void append_checksum(char *buf, size_t len, size_t pos);

// Degrees to NMEA ddmm.mmmm / dddmm.mmmm plus hemisphere character
static void nmea_coord(double deg, bool is_lat, char *out, size_t n, char *hemi) {
    *hemi = is_lat ? (deg >= 0 ? 'N' : 'S') : (deg >= 0 ? 'E' : 'W');
    deg = fabs(deg);
    int d = (int)deg;
    double m = (deg - d) * 60.0;
    snprintf(out, n, is_lat ? "%02d%07.4f" : "%03d%07.4f", d, m);
}

static bool utc_now(struct tm *tm) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t t = tv.tv_sec;
    gmtime_r(&t, tm);
    return tm->tm_year + 1900 >= 2024;  // clock still unset before the first fix
}

size_t nmea_format_gprmc(const gps_ownship_t *own, char *buf, size_t len) {
    struct tm tm;
    if (!own || !own->valid || !buf || !utc_now(&tm)) return 0;
    char lat[16], lng[16], ns, ew;
    nmea_coord(own->lat, true, lat, sizeof(lat), &ns);
    nmea_coord(own->lng, false, lng, sizeof(lng), &ew);
    size_t pos = snprintf(buf, len, "$GPRMC,%02d%02d%02d.00,A,%s,%c,%s,%c,%u.0,%u.0,%02d%02d%02d,,,A",
                          tm.tm_hour, tm.tm_min, tm.tm_sec, lat, ns, lng, ew,
                          (unsigned)own->speed_kt, (unsigned)own->track_deg,
                          tm.tm_mday, tm.tm_mon + 1, tm.tm_year % 100);
    if (pos + 6 >= len) return 0;
    append_checksum(buf, len, pos);
    return strlen(buf);
}

size_t nmea_format_gpgga(const gps_ownship_t *own, char *buf, size_t len) {
    struct tm tm;
    if (!own || !own->valid || !buf || !utc_now(&tm)) return 0;
    char lat[16], lng[16], ns, ew;
    nmea_coord(own->lat, true, lat, sizeof(lat), &ns);
    nmea_coord(own->lng, false, lng, sizeof(lng), &ew);
    size_t pos = snprintf(buf, len, "$GPGGA,%02d%02d%02d.00,%s,%c,%s,%c,%u,%02u,%.1f,%.1f,M,%.1f,M,,",
                          tm.tm_hour, tm.tm_min, tm.tm_sec, lat, ns, lng, ew,
                          own->fix_quality ? (unsigned)own->fix_quality : 1u,
                          (unsigned)own->num_sats, own->hdop_x10 / 10.0,
                          own->alt_msl_ft * 0.3048, own->geoid_sep_ft * 0.3048);
    if (pos + 6 >= len) return 0;
    append_checksum(buf, len, pos);
    return strlen(buf);
}
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

static void append_checksum(char *buf, size_t len, size_t pos) {
    snprintf(buf + pos, len - pos, "*%02X\r\n", nmea_checksum(buf));
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
                         const gps_ownship_t *own,
                         char *buf, size_t len) {
    if (!buf || !len || !traffic || !traffic->position_valid || !own || !own->valid) {
        return 0;
    }
    
    // Compute relative coordinates (North/East in meters)
    int32_t rel_north, rel_east;
    compute_relative_coords(traffic->lat, traffic->lng, own->lat, own->lng,
                            &rel_north, &rel_east);
    
    // Altitude difference vs ownship GPS MSL altitude (feet to meters: * 0.3048)
    int32_t rel_vertical = (int32_t)((traffic->alt_ft - own->alt_msl_ft) * 0.3048);
    
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
    
    // Standard PFLAA: ...,track,turnRate,groundSpeed(m/s),climbRate(m/s),type,noTrack,source,rssi
    // The ID carries the protocol tag after '!' (ADB = ADS-B) which SkyView reads.
    pos = snprintf(buf, len,
                   "$PFLAA,%d,%d,%d,%d,%d,%06X!ADB_%06X,%d,,%d,%.1f,%X,%d,%d,%d",
                   (int)alarm_level, (int)rel_north, (int)rel_east, (int)rel_vertical,
                   addr_type, (unsigned int)traffic->icao_addr, (unsigned int)traffic->icao_addr,
                   (int)traffic->track_deg, (int)lround(traffic->speed_kt * 0.514444),
                   traffic->vvel_fpm * 0.00508, acft_type,
                   0, data_source, (int)rssi);

    if (pos + 6 >= len) return 0;  // Buffer too small
    
    // Append checksum
    uint8_t checksum = nmea_checksum(buf);
    pos += snprintf(buf + pos, len - pos, "*%02X\r\n", checksum);
    
    return pos;
}

size_t nmea_format_pflau(size_t traffic_count, const traffic_info_t *targets, size_t n_targets,
                         const gps_ownship_t *own,
                         char *buf, size_t len) {
    if (!buf || !len) {
        return 0;
    }
    
    bool gps_valid = own && own->valid;

    // Highest priority target = nearest one with a position (needs ownship fix)
    const traffic_info_t *hp_traffic = NULL;
    int32_t hp_north = 0, hp_east = 0;
    double best_dist = 0;
    for (size_t i = 0; gps_valid && i < n_targets; i++) {
        if (!targets[i].position_valid) continue;
        int32_t n, e;
        compute_relative_coords(targets[i].lat, targets[i].lng, own->lat, own->lng, &n, &e);
        double d = hypot((double)n, (double)e);
        if (!hp_traffic || d < best_dist) {
            hp_traffic = &targets[i];
            hp_north = n;
            hp_east = e;
            best_dist = d;
        }
    }
    
    // TX status (1=transmitting, always on for Stratux)
    uint8_t tx_status = 1;
    
    // GPS status (0=no fix, 2=3D fix)
    uint8_t gps_status = gps_valid ? 2 : 0;
    
    // Power status (1=OK, always for ESP32)
    uint8_t power_status = 1;
    
    // Alarm level of highest priority traffic (0 for MVP)
    uint8_t alarm_level = 0;
    
    // Relative bearing to HP target, degrees from our track (-180..180)
    char rel_bearing_str[8] = "";
    if (hp_traffic) {
        int b = (int)lround(atan2((double)hp_east, (double)hp_north) * 180.0 / M_PI) - (int)own->track_deg;
        while (b > 180) b -= 360;
        while (b < -180) b += 360;
        snprintf(rel_bearing_str, sizeof(rel_bearing_str), "%d", b);
    }
    
    // Alarm type (0=no alarm, 2=aircraft)
    uint8_t alarm_type = alarm_level > 0 ? 2 : 0;
    
    // HP target altitude difference (meters, relative to ownship GPS MSL altitude)
    int32_t hp_alt_diff = hp_traffic ? (int32_t)((hp_traffic->alt_ft - own->alt_msl_ft) * 0.3048) : 0;
    
    // HP target distance (meters)
    uint32_t hp_distance = (uint32_t)best_dist;
    
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
