#pragma once
#include <WiFi.h>

// The callback only records a scalar; all Serial output stays on loop's task.
static volatile bool captureWiFiDisconnect = false;
static volatile uint8_t lastWiFiDisconnectReason = 0;
static bool wifiDiagnosticsRegistered = false;
static bool wifiRetryPending = false;
static unsigned long wifiFailedAt = 0;
static unsigned long wifiRetryDelayMs = 5000;
static unsigned long wifiNextRetryDelayMs = 5000;

static bool wifiRetryDue() {
  return !wifiRetryPending ||
         (unsigned long)(millis() - wifiFailedAt) >= wifiRetryDelayMs;
}

static void prepareWiFiDiagnostics() {
  captureWiFiDisconnect = false;
  if (!wifiDiagnosticsRegistered) {
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
      if (captureWiFiDisconnect) {
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        // Reason 8 is a voluntary disconnect during reset/internal retries.
        if (reason != 8) {
          lastWiFiDisconnectReason = reason;
        }
      }
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    wifiDiagnosticsRegistered = true;
  }
  // Application owns retries, avoiding competing automatic reconnect attempts.
  WiFi.setAutoReconnect(false);
}

static void scanConfiguredWiFi(const char* ssid) {
  Serial.println("Checking visibility of configured Wi-Fi (2.4 GHz)...");
  int count = WiFi.scanNetworks(false, true);
  bool found = false;
  for (int i = 0; i < count; ++i) {
    if (WiFi.SSID(i) == ssid) {
      found = true;
      Serial.printf("Configured network visible: channel=%ld RSSI=%ld dBm auth=%d\n",
                    (long)WiFi.channel(i), (long)WiFi.RSSI(i),
                    (int)WiFi.encryptionType(i));
    }
  }
  if (count < 0) {
    Serial.printf("Wi-Fi scan failed (%d); connection will still be attempted.\n", count);
  } else if (!found) {
    Serial.println("Configured network not seen in scan; hidden SSIDs may still connect.");
  }
  WiFi.scanDelete();
  lastWiFiDisconnectReason = 0;
  captureWiFiDisconnect = true;
}

static void reportWiFiFailure() {
  const uint8_t reason = lastWiFiDisconnectReason;
  if (reason != 0) {
    Serial.printf("Wi-Fi disconnect reason: %u (%s)\n", (unsigned int)reason,
                  WiFi.STA.disconnectReasonName((wifi_err_reason_t)reason));
    if (reason == 15 || reason == 202 || reason == 204) {
      Serial.println("Authentication/handshake failed: check password and router access settings.");
    } else if (reason == 201) {
      Serial.println("No suitable access point found: check SSID, 2.4 GHz signal and antenna.");
    }
  } else {
    Serial.println("No disconnect event captured: connection or DHCP may have timed out.");
  }
  // End the timed-out attempt so it cannot connect later during backoff
  // without running the sketch's UDP initialization on the next success.
  captureWiFiDisconnect = false;
  WiFi.disconnect(false, false);
  // Keep the private sensor AP running while the internet uplink retries.
  WiFi.mode(WIFI_AP);
  wifiRetryDelayMs = wifiNextRetryDelayMs;
  wifiFailedAt = millis();
  wifiRetryPending = true;
  wifiNextRetryDelayMs = wifiRetryDelayMs < 30000
                          ? wifiRetryDelayMs * 2 : 30000;
  if (wifiNextRetryDelayMs > 30000) wifiNextRetryDelayMs = 30000;
  Serial.printf("Next Wi-Fi attempt in %lu seconds.\n", wifiRetryDelayMs / 1000);
}

static void reportWiFiSuccess() {
  wifiRetryPending = false;
  wifiNextRetryDelayMs = 5000;
}
