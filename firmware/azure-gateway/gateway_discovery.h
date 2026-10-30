#pragma once
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "../shared/collection_config.h"

static bool replyToGatewayDiscovery(WiFiUDP& udp, const String& payload) {
  JsonDocument request;
  if (deserializeJson(request, payload) ||
      request["type"] != "discover_gateway" || request["version"] != 1) return false;
  IPAddress peer = udp.remoteIP();
  uint16_t port = udp.remotePort();
  JsonDocument reply;
  reply["type"] = "gateway_discovery";
  reply["gateway_id"] = EXPECTED_GATEWAY_ID;
  reply["port"] = GATEWAY_UDP_PORT;
  String body;
  serializeJson(reply, body);
  udp.beginPacket(peer, port);
  udp.print(body);
  udp.endPacket();
  return true;
}
