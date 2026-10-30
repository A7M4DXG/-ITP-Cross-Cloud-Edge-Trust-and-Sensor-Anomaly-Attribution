#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../shared/collection_config.h"

class CollectionApproval {
 public:
  void reset(const char* bootId) {
    strncpy(bootId_, bootId ? bootId : "", sizeof(bootId_) - 1);
    bootId_[sizeof(bootId_) - 1] = 0;
    enabled_ = false;
    expiry_ = 0;
  }
  bool apply(bool enabled, const char* requestId, uint64_t expiry, uint64_t now) {
    enabled_ = false;
    expiry_ = 0;
    if (!enabled || now < MIN_VALID_UTC || !requestId || strlen(bootId_) != 32 ||
        strcmp(bootId_, requestId) != 0 || expiry <= now ||
        expiry - now > MAX_APPROVAL_SECONDS) return false;
    enabled_ = true;
    expiry_ = expiry;
    return true;
  }
  bool allowed(uint64_t now) const {
    return enabled_ && now >= MIN_VALID_UTC && now < expiry_;
  }
  const char* bootId() const { return bootId_; }
 private:
  char bootId_[33] = {};
  bool enabled_ = false;
  uint64_t expiry_ = 0;
};

static uint32_t telemetryUnits(size_t bytes) {
  return bytes == 0 ? 0 : (uint32_t)((bytes + 511) / 512);
}
static bool canReserveTelemetry(uint32_t used, uint32_t units) {
  return units != 0 && used <= MAX_TELEMETRY_UNITS_PER_UTC_DAY &&
         units <= MAX_TELEMETRY_UNITS_PER_UTC_DAY - used;
}

class ForwardCadence {
 public:
  bool ready(int index, uint32_t now) const {
    return index >= 0 && (size_t)index < SENSOR_NODE_COUNT &&
           (!seen_[index] || (uint32_t)(now - last_[index]) >= MIN_FORWARD_INTERVAL_MS);
  }
  void record(int index, uint32_t now) {
    if (index < 0 || (size_t)index >= SENSOR_NODE_COUNT) return;
    last_[index] = now;
    seen_[index] = true;
  }
 private:
  uint32_t last_[SENSOR_NODE_COUNT] = {};
  bool seen_[SENSOR_NODE_COUNT] = {};
};
