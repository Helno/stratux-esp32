#pragma once
#include <stddef.h>

// FreeRTOS task for the ADSBee m1421 dual-band (1090ES + 978 UAT) receiver connected directly to
// the Pong's UART pins. Sets the module to AIRCRAFT_JSON_UAT_UPLINK output (1,000,000 baud),
// feeds the aircraft it reports into the traffic table and hands 978 uplinks (FIS-B) to the UAT
// decoder. Started instead of pong_rx_task when the m1421_en setting is on.
void m1421_rx_task(void *arg);

// Raw feed: the last lines received from the module (aircraft/uplink JSON, command replies).
size_t m1421_diag_copy(char *out, size_t cap);
