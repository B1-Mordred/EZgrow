#pragma once
#include <Arduino.h>

// Initialise history persistence.
//
// Call once from setup(), *after* initHardware() so that:
// - LittleFS is mounted
// - NTP/time, sensors and initial config are ready
//
// If a valid history file exists on LittleFS, this will populate:
// - gHistoryBuf
// - gHistoryIndex
// - gHistoryFull
// so that /api/history and the dashboard charts can show data
// collected before the last reboot.
void initHistoryStorage();

// Periodic persistence hook.
//
// Call regularly from loop(), ideally *after* logHistorySample().
// This function will, at a safe interval aligned with HISTORY_INTERVAL_MS,
// rewrite a compact binary file with:
//
// - a small header (magic, version, size, interval)
// - gHistoryIndex
// - gHistoryFull
// - gHistoryBuf[HISTORY_SIZE]
//
// so the last 7 days survive power cycles and restarts.
void historyStorageLoop();
