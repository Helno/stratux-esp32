#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef CONFIG_BT_BLE_ENABLED

// NMEA over BLE: transmits traffic data as FLARM/NMEA sentences over Bluetooth Low Energy.
// Compatible with SoftRF-Moshe Braner fork and FLARM-aware EFBs (XCSoar, SeeYou, etc).
// Uses SoftRF standard UUIDs: Service 0xffe0, Characteristic 0xffe1.

// Initialize BLE GATT server and NMEA formatter.
// Call once in app_main before starting nmea_ble_start().
void nmea_ble_init(void);

// Start the NMEA BLE task (sends traffic as FLARM/NMEA sentences to connected clients).
// Call after nmea_ble_init(). Task runs at 1 Hz, polling traffic table.
// If BLE is not enabled in settings, task runs but doesn't advertise.
void nmea_ble_start(void);

// Get current BLE connection status.
bool nmea_ble_is_connected(void);

// Get count of connected BLE clients.
uint8_t nmea_ble_client_count(void);

#else  // CONFIG_BT_BLE_ENABLED

// Stub implementations when BLE is disabled
static inline void nmea_ble_init(void) {}
static inline void nmea_ble_start(void) {}
static inline bool nmea_ble_is_connected(void) { return false; }
static inline uint8_t nmea_ble_client_count(void) { return 0; }

#endif  // CONFIG_BT_BLE_ENABLED
