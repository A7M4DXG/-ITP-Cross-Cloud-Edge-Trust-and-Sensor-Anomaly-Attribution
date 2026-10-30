#pragma once
#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <esp_system.h>
#include <time.h>
#include "collection_policy.h"

class GatewayControl {
 public:
  explicit GatewayControl(PubSubClient& mqtt) : mqtt_(mqtt) {}

  void begin() {
    storageReady_ = preferences_.begin("itp-budget", false);
    uint32_t bootCounter = 0;
    if (storageReady_) {
      bootCounter = preferences_.getUInt("boots", 0) + 1;
      if (bootCounter == 0 || preferences_.putUInt("boots", bootCounter) != sizeof(uint32_t) ||
          preferences_.getUInt("boots", 0) != bootCounter)
        storageReady_ = false;
    }
    char request[33];
    snprintf(request, sizeof(request), "%08lx%08lx%08lx%08lx",
             (unsigned long)bootCounter, (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random());
    approval_.reset(request);
    if (storageReady_) {
      utcDay_ = preferences_.getUInt("day", 0);
      unitsUsed_ = preferences_.getUInt("units", 0);
    }
    Serial.printf("Approval request ID for this boot: %s\n", request);
    Serial.println("Collection defaults to DENIED until this boot is approved in the device twin.");
    if (!storageReady_) Serial.println("ERROR: durable quota storage unavailable; collection denied.");
  }

  void onConnected() {
    synchronized_ = false; // A reconnect must fetch the current desired state.
    dirty_ = true;
    twinAttempts_ = 0;
    requestTwin();
  }

  void onMessage(char* topic, byte* payload, unsigned int length) {
    const bool full = strncmp(topic, "$iothub/twin/res/200/", sizeof("$iothub/twin/res/200/") - 1) == 0;
    const bool patch = strncmp(topic, "$iothub/twin/PATCH/properties/desired/", sizeof("$iothub/twin/PATCH/properties/desired/") - 1) == 0;
    if (!full && !patch) {
      if (strncmp(topic, "$iothub/twin/res/", 17) == 0 &&
          strncmp(topic, "$iothub/twin/res/204/", 21) != 0) {
        Serial.printf("Twin response: %s\n", topic);
      }
      return;
    }
    JsonDocument document;
    if (deserializeJson(document, (const byte*)payload, length)) {
      synchronized_ = false;
      dirty_ = true;
      Serial.println("Invalid twin JSON; collection denied.");
      return;
    }
    JsonObjectConst desired = full ? document["desired"].as<JsonObjectConst>()
                                  : document.as<JsonObjectConst>();
    if (desired.isNull()) {
      // A successful reported-property response has no desired object.
      return;
    }
    uint32_t version = desired["$version"] | 0U;
    if (version >= desiredVersion_) {
      desiredVersion_ = version;
      if (full || desired.containsKey("collection")) {
        JsonVariantConst collection = desired["collection"];
        if (collection.isNull() || !collection.is<JsonObjectConst>()) {
          desiredEnabled_ = false;
          desiredRequestId_ = "";
          desiredExpiry_ = "";
        } else {
          if (full || collection.as<JsonObjectConst>().containsKey("enabled"))
            desiredEnabled_ = collection["enabled"].is<bool>() && collection["enabled"].as<bool>();
          if (full || collection.as<JsonObjectConst>().containsKey("requestId"))
            desiredRequestId_ = collection["requestId"].is<const char*>()
                                 ? collection["requestId"].as<const char*>() : "";
          if (full || collection.as<JsonObjectConst>().containsKey("expiresAtUtc"))
            desiredExpiry_ = collection["expiresAtUtc"].is<const char*>()
                              ? collection["expiresAtUtc"].as<const char*>() : "";
        }
      }
      approval_.apply(desiredEnabled_, desiredRequestId_.c_str(),
                      parseUtc(desiredExpiry_.c_str()), (uint64_t)time(nullptr));
    }
    if (full) synchronized_ = true;
    dirty_ = true;
  }

  void tick() {
    if (!mqtt_.connected()) { synchronized_ = false; return; }
    refreshBudgetDay();
    if (!synchronized_ && twinAttempts_ < 6) {
      unsigned int shift = twinAttempts_ > 0 ? twinAttempts_ - 1 : 0;
      if (shift > 3) shift = 3;
      unsigned long retryMs = 30000UL << shift;
      if ((unsigned long)(millis() - lastTwinRequestAt_) >= retryMs) requestTwin();
    }
    const bool allowed = synchronized_ && storageReady_ &&
                         unitsUsed_ < MAX_TELEMETRY_UNITS_PER_UTC_DAY &&
                         approval_.allowed((uint64_t)time(nullptr));
    if (allowed != lastAllowed_) {
      lastAllowed_ = allowed;
      dirty_ = true;
      Serial.println(allowed ? "COLLECTION APPROVED for this boot."
                             : "COLLECTION STOPPED by approval or quota gate.");
    }
    if (dirty_ || (unsigned long)(millis() - lastReportAt_) >= STATUS_REPORT_INTERVAL_MS)
      reportState();
  }

  bool beforePublish(int sensorIndex, size_t bodyBytes) {
    if (!mqtt_.connected() || !synchronized_ ||
        !approval_.allowed((uint64_t)time(nullptr))) {
      ++approvalBlocked_;
      return false;
    }
    if (sensorIndex < 0 || (size_t)sensorIndex >= SENSOR_NODE_COUNT) { ++unknownBlocked_; return false; }
    if (bodyBytes == 0 || bodyBytes > MAX_TELEMETRY_BODY_BYTES) {
      ++oversizeBlocked_;
      Serial.println("Body exceeds the one-unit F1 contract; not published.");
      return false;
    }
    const unsigned long nowMs = millis();
    if (!cadence_.ready(sensorIndex, nowMs)) {
      ++rateBlocked_;
      return false;
    }
    if (!refreshBudgetDay()) return false;
    uint32_t units = telemetryUnits(bodyBytes);
    if (!canReserveTelemetry(unitsUsed_, units)) {
      return false;
    }
    // Reserve BEFORE sending. Failed sends are conservatively charged locally;
    // reboots cannot reset this counter or cause an optimistic overspend.
    uint32_t next = unitsUsed_ + units;
    if (preferences_.putUInt("units", next) != sizeof(uint32_t) ||
        preferences_.getUInt("units", UINT32_MAX) != next) {
      storageReady_ = false;
      dirty_ = true;
      return false;
    }
    unitsUsed_ = next;
    if (unitsUsed_ >= MAX_TELEMETRY_UNITS_PER_UTC_DAY) dirty_ = true;
    cadence_.record(sensorIndex, nowMs);
    return true;
  }

  void publishCompleted(bool success, int index) {
    if (success) {
      ++published_;
      if (index >= 0 && (size_t)index < SENSOR_NODE_COUNT && ++sourcePublished_[index] == 1) dirty_ = true;
    }
    else ++publishFailures_;
  }

 private:
  PubSubClient& mqtt_;
  Preferences preferences_;
  CollectionApproval approval_;
  bool storageReady_ = false;
  bool synchronized_ = false;
  bool dirty_ = true;
  bool lastAllowed_ = false;
  bool desiredEnabled_ = false;
  uint32_t desiredVersion_ = 0;
  String desiredRequestId_;
  String desiredExpiry_;
  uint32_t utcDay_ = 0;
  uint32_t unitsUsed_ = 0;
  unsigned long lastReportAt_ = 0;
  unsigned long lastTwinRequestAt_ = 0;
  unsigned int twinAttempts_ = 0;
  ForwardCadence cadence_;
  uint32_t sourcePublished_[SENSOR_NODE_COUNT] = {};
  uint32_t approvalBlocked_ = 0, unknownBlocked_ = 0, oversizeBlocked_ = 0;
  uint32_t rateBlocked_ = 0, published_ = 0, publishFailures_ = 0;

  void requestTwin() {
    lastTwinRequestAt_ = millis();
    ++twinAttempts_;
    bool replies = mqtt_.subscribe("$iothub/twin/res/#", 1);
    bool desired = mqtt_.subscribe("$iothub/twin/PATCH/properties/desired/#", 1);
    if (!replies || !desired || !mqtt_.publish("$iothub/twin/GET/?$rid=sync", "")) {
      Serial.println("Twin sync could not start; collection denied. Retry is bounded.");
    }
  }

  static uint64_t parseUtc(const char* text) {
    if (!text || strlen(text) != 20) return 0;
    struct tm parsed = {};
    char* end = strptime(text, "%Y-%m-%dT%H:%M:%SZ", &parsed);
    if (!end || *end) return 0;
    parsed.tm_isdst = 0;
    // Gateway configTime uses offset 0 and no DST, so mktime is UTC here.
    time_t value = mktime(&parsed);
    if (value < (time_t)MIN_VALID_UTC) return 0;
    struct tm utc = {};
    gmtime_r(&value, &utc);
    char check[21];
    strftime(check, sizeof(check), "%Y-%m-%dT%H:%M:%SZ", &utc);
    if (strcmp(check, text) != 0) return 0; // Reject normalized invalid dates.
    return (uint64_t)value;
  }

  bool refreshBudgetDay() {
    if (!storageReady_ || time(nullptr) < (time_t)MIN_VALID_UTC) return false;
    uint32_t day = (uint64_t)time(nullptr) / 86400;
    if (day < utcDay_) return false; // Never grant fresh budget after clock rollback.
    if (day != utcDay_) {
      // Clear units BEFORE advancing the day. A power cut here can only cause
      // another reset at the NEW UTC day, not reset today's established ledger.
      if (preferences_.putUInt("units", 0) != sizeof(uint32_t) ||
          preferences_.putUInt("day", day) != sizeof(uint32_t)) {
        storageReady_ = false;
        return false;
      }
      unitsUsed_ = 0;
      utcDay_ = day;
    }
    return true;
  }

  void reportState() {
    const bool allowed = synchronized_ && approval_.allowed((uint64_t)time(nullptr));
    const char* state = !storageReady_ ? "quota_storage_unavailable" :
                        unitsUsed_ >= MAX_TELEMETRY_UNITS_PER_UTC_DAY ? "daily_budget_reached" :
                        allowed ? "collecting" : "awaiting_approval";
    JsonDocument document;
    document["firmware"] = "itp-azure-3nodes-v1";
    JsonObject collection = document["collection"].to<JsonObject>();
    collection["requestId"] = approval_.bootId();
    collection["state"] = state;
    collection["expiresAtUtc"] = desiredExpiry_;
    collection["samplingIntervalSeconds"] = SENSOR_SAMPLING_INTERVAL_MS / 1000;
    collection["telemetryUnitsToday"] = unitsUsed_;
    collection["telemetryDailyLimit"] = MAX_TELEMETRY_UNITS_PER_UTC_DAY;
    collection["forwardedThisBoot"] = published_;
    collection["publishFailures"] = publishFailures_;
    collection["approvalBlocked"] = approvalBlocked_;
    collection["rateBlocked"] = rateBlocked_;
    collection["unknownSourceBlocked"] = unknownBlocked_;
    collection["oversizeBlocked"] = oversizeBlocked_;
    JsonObject sources = collection["publishAcceptedBySource"].to<JsonObject>();
    sources["sensor-node-01"] = sourcePublished_[0];
    sources["sensor-node-02"] = sourcePublished_[1];
    sources["sensor-node-03"] = sourcePublished_[2];
    String payload;
    serializeJson(document, payload);
    if (mqtt_.publish("$iothub/twin/PATCH/properties/reported/?$rid=status", payload.c_str())) {
      lastReportAt_ = millis();
      dirty_ = false;
    }
  }
};
