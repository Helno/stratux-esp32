#pragma once
#include <stddef.h>
#include "pong_frame.h"   // pong_frame_t / pong_line_kind_t (shared format)

// FreeRTOS task: configure UART2 for M1421 ADS-B receiver, auto-detect baud rate,
// read Beast binary frames, decode to pong_frame_t (1090ES only; M1421 doesn't do UAT),
// and dispatch to modes decoder.
//
// M1421 protocol: 9600-460800 baud, 8N1. Beast binary format with 0x1A escape.
// On "ERROR" or detection failure, the task must perform real recovery
// (reopen UART / toggle a reset GPIO / mark degraded).
void m1421_rx_task(void *arg);

// Auto-detection: probe for M1421 on UART2 with timeout.
// Returns true if M1421 is detected and ready, false otherwise.
bool m1421_detect(void);

// Diagnostic ring (same as pong): every received Beast frame (hex repr)
// is kept in a ring for web UI debugging.
size_t m1421_diag_copy(char *out, size_t cap);
