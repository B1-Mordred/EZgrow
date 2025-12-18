#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#include "Greenhouse.h"
#include "HistoryStorage.h"

// Path on LittleFS where the 7-day ring buffer is stored.
static const char* HISTORY_FILE_PATH = "/history.bin";

// Simple header to sanity-check the on-disk layout.
struct HistoryFileHeader {
  uint32_t magic;            // identifies the file
  uint16_t version;          // layout version
  uint16_t reserved;         // padding / future use
  uint32_t historySize;      // must match HISTORY_SIZE
  uint32_t historyIntervalMs; // must match HISTORY_INTERVAL_MS
};

// Chosen to be clearly non-accidental in flash.
static const uint32_t HISTORY_MAGIC   = 0x485A4737; // 'HZG7'
static const uint16_t HISTORY_VERSION = 1;

static bool         sHistoryStorageReady = false;
static unsigned long sLastHistorySaveMs  = 0;

// Forward declaration
static void saveHistoryNow();

void initHistoryStorage() {
  // Make sure LittleFS is mounted. This is idempotent and will
  // usually be a fast no-op if initHardware() already did it.
  if (!LittleFS.begin()) {
    Serial.println("[HISTFS] LittleFS.begin() failed; history persistence DISABLED.");
    return;
  }

  sHistoryStorageReady = true;

  File f = LittleFS.open(HISTORY_FILE_PATH, "r");
  if (!f) {
    Serial.println("[HISTFS] No existing history file; starting with empty 7-day buffer.");
    return;
  }

  HistoryFileHeader hdr;
  if (f.readBytes(reinterpret_cast<char*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
    Serial.println("[HISTFS] Failed to read history header; ignoring file.");
    f.close();
    return;
  }

  if (hdr.magic != HISTORY_MAGIC ||
      hdr.version != HISTORY_VERSION ||
      hdr.historySize != static_cast<uint32_t>(HISTORY_SIZE) ||
      hdr.historyIntervalMs != static_cast<uint32_t>(HISTORY_INTERVAL_MS)) {
    Serial.println("[HISTFS] History header mismatch (magic/version/size/interval); ignoring file.");
    f.close();
    return;
  }

  size_t idx  = 0;
  bool   full = false;

  if (f.readBytes(reinterpret_cast<char*>(&idx), sizeof(idx)) != sizeof(idx)) {
    Serial.println("[HISTFS] Failed to read history index; ignoring file.");
    f.close();
    return;
  }

  if (f.readBytes(reinterpret_cast<char*>(&full), sizeof(full)) != sizeof(full)) {
    Serial.println("[HISTFS] Failed to read history full flag; ignoring file.");
    f.close();
    return;
  }

  const size_t bufBytes = sizeof(gHistoryBuf);
  if (f.readBytes(reinterpret_cast<char*>(gHistoryBuf), bufBytes) != static_cast<int>(bufBytes)) {
    Serial.println("[HISTFS] Failed to read history buffer; ignoring file.");
    f.close();
    return;
  }

  // Basic sanity on metadata
  if (idx > HISTORY_SIZE) {
    Serial.println("[HISTFS] Stored index out of range; resetting to 0.");
    idx  = 0;
    full = false;
  }

  gHistoryIndex = idx;
  gHistoryFull  = full && (gHistoryIndex <= HISTORY_SIZE);

  f.close();

  const size_t count = gHistoryFull ? HISTORY_SIZE : gHistoryIndex;
  Serial.print("[HISTFS] Loaded ");
  Serial.print(count);
  Serial.println(" historical samples from LittleFS.");

  // Start save timer from "now" so we do not immediately re-write.
  sLastHistorySaveMs = millis();
}

static void saveHistoryNow() {
  if (!sHistoryStorageReady) {
    return;
  }

  File f = LittleFS.open(HISTORY_FILE_PATH, "w");
  if (!f) {
    Serial.println("[HISTFS] Failed to open history file for write.");
    return;
  }

  HistoryFileHeader hdr;
  hdr.magic           = HISTORY_MAGIC;
  hdr.version         = HISTORY_VERSION;
  hdr.reserved        = 0;
  hdr.historySize     = static_cast<uint32_t>(HISTORY_SIZE);
  hdr.historyIntervalMs = static_cast<uint32_t>(HISTORY_INTERVAL_MS);

  if (f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
    Serial.println("[HISTFS] Failed to write history header.");
    f.close();
    return;
  }

  if (f.write(reinterpret_cast<const uint8_t*>(&gHistoryIndex),
              sizeof(gHistoryIndex)) != sizeof(gHistoryIndex)) {
    Serial.println("[HISTFS] Failed to write history index.");
    f.close();
    return;
  }

  if (f.write(reinterpret_cast<const uint8_t*>(&gHistoryFull),
              sizeof(gHistoryFull)) != sizeof(gHistoryFull)) {
    Serial.println("[HISTFS] Failed to write history full flag.");
    f.close();
    return;
  }

  const size_t bufBytes = sizeof(gHistoryBuf);
  if (f.write(reinterpret_cast<const uint8_t*>(gHistoryBuf), bufBytes) != bufBytes) {
    Serial.println("[HISTFS] Failed to write history buffer.");
    f.close();
    return;
  }

  f.close();
  Serial.println("[HISTFS] History written to LittleFS.");
}

void historyStorageLoop() {
  if (!sHistoryStorageReady) {
    return;
  }

  const unsigned long now = millis();
  const unsigned long intervalMs = HISTORY_INTERVAL_MS; // 10 minutes (for 7-day window)

  // Save roughly once per HISTORY_INTERVAL_MS to:
  // - keep flash wear reasonable (~6 writes/hour)
  // - ensure at most ~10 minutes of samples are lost on power failure
  if (sLastHistorySaveMs == 0 || (now - sLastHistorySaveMs) >= intervalMs) {
    sLastHistorySaveMs = now;
    saveHistoryNow();
  }
}
