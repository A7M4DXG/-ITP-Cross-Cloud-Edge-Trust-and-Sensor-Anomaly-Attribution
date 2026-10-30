#pragma once
#include <WiFi.h>
#include <WiFiUdp.h>
#include "collection_config.h"

// The gateway owns this private LAN. Uplink DHCP changes never change its
// sensor-facing address; nodes do not depend on router/hotspot multicast.
static bool discoverAzureGateway(WiFiUDP&, IPAddress& result) {
  if (WiFi.status() != WL_CONNECTED) return false;
  IPAddress expected;
  expected.fromString(SENSOR_GATEWAY_IPV4);
  if (WiFi.SSID() != SENSOR_WIFI_SSID || WiFi.gatewayIP() != expected) {
    Serial.println("Unexpected sensor network or DHCP gateway; not sending.");
    return false;
  }
  result = expected;
  return true;
}
