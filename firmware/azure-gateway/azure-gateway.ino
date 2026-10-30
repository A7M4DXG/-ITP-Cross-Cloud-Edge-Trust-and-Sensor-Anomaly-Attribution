#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <time.h>

#include "secrets.h"
#include "wifi_diagnostics.h"
#include "sensor_registry.h"
#include "gateway_control.h"
#include "gateway_discovery.h"
#include <ESPmDNS.h>
#include "azure_root_ca.h"

#include "mbedtls/base64.h"
#include "mbedtls/md.h"

// ============================================================
// PROJECT
// Cross-Cloud Edge Trust and Sensor Anomaly Attribution
//
// Device:
// XIAO ESP32-C3 - Azure Gateway
//
// Flow:
// Sensor Nodes #1, #2 and #3
//      |
//      | UDP
//      v
// Azure Gateway
//      |
//      | MQTT + TLS + SAS
//      v
// Azure IoT Hub
//
// Phase 2:
// Formal telemetry + provenance + gateway validation
// ============================================================


// ============================================================
// WIFI
// ============================================================

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
GatewayControl gatewayControl(mqttClient);


// ============================================================
// UDP
// ============================================================

WiFiUDP udp;

const unsigned int UDP_LISTEN_PORT = 5000;


// ============================================================
// AZURE
// ============================================================

const char* AZURE_MQTT_TOPIC =
    "devices/azure-gateway-01/messages/events/";


// ============================================================
// DEVICE IDENTITIES
// ============================================================

const char* GATEWAY_ID = "azure-gateway-01";

// Accepted sources are listed in sensor_registry.h.


// ============================================================
// STATE
// ============================================================

unsigned long packetCount = 0;

unsigned long acceptedCount = 0;
unsigned long rejectedCount = 0;

unsigned long replayCandidateCount = 0;
unsigned long sequenceGapCount = 0;

// Each sensor has its own sequence state in sensor_registry.h.


// ============================================================
// TIME
// ============================================================

const char* NTP_SERVER = "pool.ntp.org";

const long GMT_OFFSET_SEC = 0;
const int DAYLIGHT_OFFSET_SEC = 0;


// ============================================================
// HELPER: PRINT SEPARATOR
// ============================================================

void printSeparator() {
  Serial.println("--------------------------------");
}


// ============================================================
// HELPER: GET UTC ISO TIMESTAMP
// ============================================================

String getUtcTimestamp() {

  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    return "TIME_UNAVAILABLE";
  }

  char buffer[30];

  strftime(
    buffer,
    sizeof(buffer),
    "%Y-%m-%dT%H:%M:%SZ",
    &timeinfo
  );

  return String(buffer);
}


// ============================================================
// HELPER: URL ENCODE
// ============================================================

String urlEncode(const String& input) {

  String encoded = "";

  const char* hex = "0123456789ABCDEF";

  for (size_t i = 0; i < input.length(); i++) {

    char c = input.charAt(i);

    if (
      (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '-' ||
      c == '_' ||
      c == '.' ||
      c == '~'
    ) {

      encoded += c;

    } else {

      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}


// ============================================================
// HELPER: BASE64 ENCODE
// ============================================================

String base64Encode(
  const unsigned char* input,
  size_t inputLength
) {

  size_t outputLength = 0;

  size_t bufferLength =
      ((inputLength + 2) / 3) * 4 + 1;

  unsigned char* output =
      new unsigned char[bufferLength];

  if (output == nullptr) {
    return "";
  }

  int result = mbedtls_base64_encode(
    output,
    bufferLength,
    &outputLength,
    input,
    inputLength
  );

  if (result != 0) {
    delete[] output;
    return "";
  }

  output[outputLength] = '\0';

  String encoded =
      String((char*)output);

  delete[] output;

  return encoded;
}


// ============================================================
// HELPER: BASE64 DECODE
// ============================================================

bool base64Decode(
  const String& input,
  unsigned char* output,
  size_t outputSize,
  size_t& decodedLength
) {

  int result = mbedtls_base64_decode(
    output,
    outputSize,
    &decodedLength,
    (const unsigned char*)input.c_str(),
    input.length()
  );

  return result == 0;
}


// ============================================================
// HELPER: HMAC-SHA256
// ============================================================

bool calculateHmacSha256(
  const unsigned char* key,
  size_t keyLength,
  const unsigned char* data,
  size_t dataLength,
  unsigned char* output
) {

  const mbedtls_md_info_t* mdInfo =
      mbedtls_md_info_from_type(
        MBEDTLS_MD_SHA256
      );

  if (mdInfo == nullptr) {
    return false;
  }

  int result = mbedtls_md_hmac(
    mdInfo,
    key,
    keyLength,
    data,
    dataLength,
    output
  );

  return result == 0;
}


// ============================================================
// GENERATE AZURE SAS TOKEN
// ============================================================

String generateSasToken() {

  Serial.println("Generating SAS token...");

  String resourceUri =
      String(AZURE_HUB_HOST) +
      "/devices/" +
      String(AZURE_DEVICE_ID);

  String encodedResourceUri =
      urlEncode(resourceUri);

  unsigned long expiry =
      (unsigned long)time(nullptr) + 3600;

  String stringToSign =
      encodedResourceUri +
      "\n" +
      String(expiry);

  unsigned char decodedKey[128];

  size_t decodedKeyLength = 0;

  if (!base64Decode(
        String(AZURE_DEVICE_KEY),
        decodedKey,
        sizeof(decodedKey),
        decodedKeyLength
      )) {

    Serial.println(
      "ERROR: Failed to decode Azure device key."
    );

    return "";
  }

  unsigned char hmacResult[32];

  if (!calculateHmacSha256(
        decodedKey,
        decodedKeyLength,
        (const unsigned char*)stringToSign.c_str(),
        stringToSign.length(),
        hmacResult
      )) {

    Serial.println(
      "ERROR: HMAC-SHA256 calculation failed."
    );

    return "";
  }

  String encodedSignature =
      base64Encode(
        hmacResult,
        sizeof(hmacResult)
      );

  if (encodedSignature.length() == 0) {

    Serial.println(
      "ERROR: Failed to encode SAS signature."
    );

    return "";
  }

  String encodedSignatureUrl =
      urlEncode(encodedSignature);

  String sasToken =
      "SharedAccessSignature sr=" +
      encodedResourceUri +
      "&sig=" +
      encodedSignatureUrl +
      "&se=" +
      String(expiry);

  Serial.println("SAS token generated.");

  return sasToken;
}


// ============================================================
// CONNECT TO AZURE MQTT
// ============================================================

bool connectToAzure() {

  // SAS expiry and TLS validation require a usable UTC clock.
  if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) {
    return false;
  }

  if (mqttClient.connected()) {
    return true;
  }

  Serial.println();
  Serial.println("================================");
  Serial.println("   CONNECTING TO AZURE MQTT");
  Serial.println("================================");

  Serial.print("Host: ");
  Serial.println(AZURE_HUB_HOST);

  Serial.print("Port: ");
  Serial.println(8883);

  Serial.print("Device ID: ");
  Serial.println(AZURE_DEVICE_ID);

  Serial.println();

  String sasToken =
      generateSasToken();

  if (sasToken.length() == 0) {
    return false;
  }

  String username =
      String(AZURE_HUB_HOST) +
      "/" +
      String(AZURE_DEVICE_ID) +
      "/?api-version=2021-04-12";

  String clientId =
      String(AZURE_DEVICE_ID);

  Serial.println("Connecting MQTT...");

  bool connected =
      mqttClient.connect(
        clientId.c_str(),
        username.c_str(),
        sasToken.c_str()
      );

  if (connected) {

    Serial.println();
    Serial.println("================================");
    Serial.println("   AZURE MQTT CONNECTED!");
    Serial.println("================================");

    Serial.println();
    Serial.println("Device authentication: PASS");
    Serial.println("MQTT connection: PASS");
    gatewayControl.onConnected();

    return true;

  } else {

    Serial.print(
      "Azure MQTT connection FAILED. MQTT state: "
    );

    Serial.println(
      mqttClient.state()
    );

    return false;
  }
}


// ============================================================
// EXTRACT STRING FIELD
//
// Simple parser intentionally used instead of ArduinoJson so
// the gateway does not require an additional library.
// ============================================================

bool extractStringField(
  const String& json,
  const String& field,
  String& value
) {

  String key =
      "\"" + field + "\"";

  int keyPosition =
      json.indexOf(key);

  if (keyPosition < 0) {
    return false;
  }

  int colonPosition =
      json.indexOf(
        ':',
        keyPosition + key.length()
      );

  if (colonPosition < 0) {
    return false;
  }

  int firstQuote =
      json.indexOf(
        '"',
        colonPosition + 1
      );

  if (firstQuote < 0) {
    return false;
  }

  int secondQuote =
      json.indexOf(
        '"',
        firstQuote + 1
      );

  if (secondQuote < 0) {
    return false;
  }

  value =
      json.substring(
        firstQuote + 1,
        secondQuote
      );

  return true;
}


// ============================================================
// EXTRACT UNSIGNED LONG FIELD
// ============================================================

bool extractUnsignedLongField(
  const String& json,
  const String& field,
  unsigned long& value
) {

  String key =
      "\"" + field + "\"";

  int keyPosition =
      json.indexOf(key);

  if (keyPosition < 0) {
    return false;
  }

  int colonPosition =
      json.indexOf(
        ':',
        keyPosition + key.length()
      );

  if (colonPosition < 0) {
    return false;
  }

  int start =
      colonPosition + 1;

  while (
    start < (int)json.length() &&
    (
      json.charAt(start) == ' ' ||
      json.charAt(start) == '\t'
    )
  ) {
    start++;
  }

  int end = start;

  while (
    end < (int)json.length() &&
    isDigit(json.charAt(end))
  ) {
    end++;
  }

  if (end == start) {
    return false;
  }

  String number =
      json.substring(start, end);

  value =
      strtoul(
        number.c_str(),
        nullptr,
        10
      );

  return true;
}


// ============================================================
// EXTRACT FLOAT FIELD
// ============================================================

bool extractFloatField(
  const String& json,
  const String& field,
  float& value
) {

  String key =
      "\"" + field + "\"";

  int keyPosition =
      json.indexOf(key);

  if (keyPosition < 0) {
    return false;
  }

  int colonPosition =
      json.indexOf(
        ':',
        keyPosition + key.length()
      );

  if (colonPosition < 0) {
    return false;
  }

  int start =
      colonPosition + 1;

  while (
    start < (int)json.length() &&
    (
      json.charAt(start) == ' ' ||
      json.charAt(start) == '\t'
    )
  ) {
    start++;
  }

  int end = start;

  while (
    end < (int)json.length()
  ) {

    char c =
        json.charAt(end);

    if (
      isDigit(c) ||
      c == '-' ||
      c == '+' ||
      c == '.' ||
      c == 'e' ||
      c == 'E'
    ) {

      end++;

    } else {

      break;
    }
  }

  if (end == start) {
    return false;
  }

  String number =
      json.substring(start, end);

  value =
      number.toFloat();

  return true;
}


// ============================================================
// VALIDATE SENSOR MESSAGE
// ============================================================

bool validateSensorMessage(
  const String& payload,

  String& sourceId,
  unsigned long& sequence,
  unsigned long& uptimeMs,

  float& temperature,
  float& humidity,

  String& sequenceStatus,
  String& measurementStatus,
  String& overallStatus
) {

  // ----------------------------------------------------------
  // Extract required fields
  // ----------------------------------------------------------

  JsonDocument sensorDocument;
  const bool jsonOk = !deserializeJson(sensorDocument, payload) &&
                      sensorDocument.is<JsonObject>();
  const bool sourceOk = jsonOk && sensorDocument["source_id"].is<const char*>() &&
                        strlen(sensorDocument["source_id"].as<const char*>()) > 0 &&
                        strlen(sensorDocument["source_id"].as<const char*>()) <= 32;
  const bool sequenceOk = jsonOk && sensorDocument["sequence"].is<unsigned long>();
  const bool uptimeOk = jsonOk && sensorDocument["uptime_ms"].is<unsigned long>();
  const bool temperatureOk = jsonOk && sensorDocument["temperature"].is<float>();
  const bool humidityOk = jsonOk && sensorDocument["humidity"].is<float>();
  if (sourceOk) sourceId = sensorDocument["source_id"].as<const char*>();
  if (sequenceOk) sequence = sensorDocument["sequence"].as<unsigned long>();
  if (uptimeOk) uptimeMs = sensorDocument["uptime_ms"].as<unsigned long>();
  if (temperatureOk) temperature = sensorDocument["temperature"].as<float>();
  if (humidityOk) humidity = sensorDocument["humidity"].as<float>();

  // ----------------------------------------------------------
  // Required field validation
  // ----------------------------------------------------------

  if (
    !sourceOk ||
    !sequenceOk ||
    !uptimeOk ||
    !temperatureOk ||
    !humidityOk ||
    !isfinite(temperature) ||
    !isfinite(humidity)
  ) {

    sequenceStatus =
        "INVALID";

    measurementStatus =
        "INVALID";

    overallStatus =
        "MALFORMED";

    return false;
  }


  // ----------------------------------------------------------
  // Source identity validation
  // ----------------------------------------------------------

  const int sensorIndex = findSensorIndex(sourceId.c_str());
  bool sourceValid = (sensorIndex >= 0);


  // ----------------------------------------------------------
  // Measurement validation
  //
  // DHT11:
  // Temperature: -40 to 80 C used as broad validity range
  // Humidity: 0 to 100 %
  // ----------------------------------------------------------

  bool temperatureValid =
      (
        temperature >= -40.0 &&
        temperature <= 80.0
      );

  bool humidityValid =
      (
        humidity >= 0.0 &&
        humidity <= 100.0
      );

  bool measurementValid =
      temperatureValid &&
      humidityValid;


  // ----------------------------------------------------------
  // Sequence validation
  // ----------------------------------------------------------

  sequenceStatus = classifySensorSequence(sensorIndex, sequence);


  // ----------------------------------------------------------
  // Measurement status
  // ----------------------------------------------------------

  if (measurementValid) {

    measurementStatus =
        "NORMAL";

  } else {

    measurementStatus =
        "OUT_OF_RANGE";
  }


  // ----------------------------------------------------------
  // Overall classification
  // ----------------------------------------------------------

  if (!sourceValid) {

    overallStatus =
        "UNEXPECTED_SOURCE";

  } else if (!measurementValid) {

    overallStatus =
        "MEASUREMENT_ANOMALY";

  } else if (
    sequenceStatus ==
    "REPLAY_OR_DUPLICATE"
  ) {

    overallStatus =
        "REPLAY_CANDIDATE";

  } else if (
    sequenceStatus ==
    "SEQUENCE_GAP"
  ) {

    overallStatus =
        "SEQUENCE_GAP";

  } else {

    overallStatus =
        "ACCEPTED";
  }


  // ----------------------------------------------------------
  // Return whether message structure is valid
  // ----------------------------------------------------------

  return true;
}


// ============================================================
// BUILD FORMAL PROVENANCE PAYLOAD
// ============================================================

String buildAzurePayload(
  const String& sourceId,
  unsigned long sequence,
  unsigned long uptimeMs,

  float temperature,
  float humidity,

  const String& gatewayTimestamp,
  const String& sourceIp,

  const String& sequenceStatus,
  const String& measurementStatus,
  const String& overallStatus
) {

  String payload = "";

  payload += "{";

  // ----------------------------------------------------------
  // Schema
  // ----------------------------------------------------------

  payload +=
      "\"schema_version\":\"1.0\",";


  // ----------------------------------------------------------
  // Sensor-generated provenance
  // ----------------------------------------------------------

  payload += "\"source\":{";

  payload +=
      "\"device_id\":\"" +
      sourceId +
      "\",";

  payload +=
      "\"sequence\":" +
      String(sequence) +
      ",";

  payload +=
      "\"uptime_ms\":" +
      String(uptimeMs);

  payload += "},";


  // ----------------------------------------------------------
  // Sensor measurements
  // ----------------------------------------------------------

  payload += "\"measurement\":{";

  payload +=
      "\"temperature\":" +
      String(temperature, 2) +
      ",";

  payload +=
      "\"humidity\":" +
      String(humidity, 2);

  payload += "},";


  // ----------------------------------------------------------
  // Gateway-observed provenance
  // ----------------------------------------------------------

  payload += "\"gateway\":{";

  payload +=
      "\"device_id\":\"" +
      String(GATEWAY_ID) +
      "\",";

  payload +=
      "\"received_at\":\"" +
      gatewayTimestamp +
      "\",";

  payload +=
      "\"source_ip\":\"" +
      sourceIp +
      "\"";

  payload += "},";


  // ----------------------------------------------------------
  // Validation / attribution
  // ----------------------------------------------------------

  payload += "\"validation\":{";

  payload +=
      "\"sequence_status\":\"" +
      sequenceStatus +
      "\",";

  payload +=
      "\"measurement_status\":\"" +
      measurementStatus +
      "\",";

  payload +=
      "\"overall_status\":\"" +
      overallStatus +
      "\"";

  payload += "}";


  payload += "}";

  return payload;
}


// ============================================================
// RECEIVE SENSOR DATA
// ============================================================

void receiveSensorData() {

  int packetSize =
      udp.parsePacket();

  if (packetSize <= 0) {
    return;
  }


  // ----------------------------------------------------------
  // Read packet
  // ----------------------------------------------------------

  char incomingPacket[512];

  int len =
      udp.read(
        incomingPacket,
        sizeof(incomingPacket) - 1
      );

  if (len <= 0) {
    return;
  }

  incomingPacket[len] =
      '\0';

  String sensorPayload =
      String(incomingPacket);


  if (replyToGatewayDiscovery(udp, sensorPayload)) return;

  packetCount++;


  // ----------------------------------------------------------
  // Source network information
  // ----------------------------------------------------------

  IPAddress sourceIpAddress =
      udp.remoteIP();

  unsigned int sourcePort =
      udp.remotePort();

  String sourceIp =
      sourceIpAddress.toString();


  // ----------------------------------------------------------
  // Display received data
  // ----------------------------------------------------------

  Serial.println();
  Serial.println("================================");
  Serial.println("    SENSOR DATA RECEIVED");
  Serial.println("================================");

  Serial.print("Source IP: ");
  Serial.println(sourceIp);

  Serial.print("Source port: ");
  Serial.println(sourcePort);

  Serial.print("Packet number: ");
  Serial.println(packetCount);

  Serial.print("Raw sensor payload: ");
  Serial.println(sensorPayload);

  printSeparator();


  // ----------------------------------------------------------
  // Parse and validate
  // ----------------------------------------------------------

  String sourceId = "";

  unsigned long sequence = 0;

  unsigned long uptimeMs = 0;

  float temperature = 0.0;

  float humidity = 0.0;

  String sequenceStatus = "";

  String measurementStatus = "";

  String overallStatus = "";


  bool structureValid =
      validateSensorMessage(
        sensorPayload,

        sourceId,
        sequence,
        uptimeMs,

        temperature,
        humidity,

        sequenceStatus,
        measurementStatus,
        overallStatus
      );


  // ----------------------------------------------------------
  // Display validation
  // ----------------------------------------------------------

  Serial.println("VALIDATION");

  Serial.print("Source ID: ");
  Serial.println(sourceId);

  Serial.println("Allowed sources: sensor-node-01, sensor-node-02, sensor-node-03");

  Serial.print("Sequence: ");
  Serial.println(sequence);

  Serial.print("Sequence status: ");
  Serial.println(sequenceStatus);

  Serial.print("Temperature: ");
  Serial.println(temperature, 2);

  Serial.print("Humidity: ");
  Serial.println(humidity, 2);

  Serial.print("Measurement status: ");
  Serial.println(measurementStatus);

  Serial.print("Overall status: ");
  Serial.println(overallStatus);

  printSeparator();


  // ----------------------------------------------------------
  // Malformed message
  // ----------------------------------------------------------

  if (!structureValid) {

    rejectedCount++;

    Serial.println(
      "MESSAGE REJECTED"
    );

    Serial.println(
      "Reason: malformed or incomplete sensor payload."
    );

    Serial.print(
      "Rejected count: "
    );

    Serial.println(
      rejectedCount
    );

    printSeparator();

    return;
  }


  // ----------------------------------------------------------
  // Sequence tracking
  // ----------------------------------------------------------

  if (
    sequenceStatus ==
    "REPLAY_OR_DUPLICATE"
  ) {

    replayCandidateCount++;

    Serial.println(
      "WARNING: Replay/duplicate candidate detected."
    );

  } else if (
    sequenceStatus ==
    "SEQUENCE_GAP"
  ) {

    sequenceGapCount++;

    Serial.println(
      "WARNING: Sequence gap detected."
    );
  }


  // ----------------------------------------------------------
  // Update latest sequence
  // ----------------------------------------------------------

  // Unknown sources and duplicate/older packets cannot advance node state.
  recordSensorSequence(findSensorIndex(sourceId.c_str()), sequence);


  // ----------------------------------------------------------
  // Build formal provenance payload
  // ----------------------------------------------------------

  String gatewayTimestamp =
      getUtcTimestamp();


  String azurePayload =
      buildAzurePayload(
        sourceId,
        sequence,
        uptimeMs,

        temperature,
        humidity,

        gatewayTimestamp,
        sourceIp,

        sequenceStatus,
        measurementStatus,
        overallStatus
      );


  // ----------------------------------------------------------
  // Display final payload
  // ----------------------------------------------------------

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "    FORMAL PROVENANCE PAYLOAD"
  );

  Serial.println(
    "================================"
  );

  Serial.println(
    azurePayload
  );

  printSeparator();


  // ----------------------------------------------------------
  // Connect to Azure if required
  // ----------------------------------------------------------

  if (!mqttClient.connected()) {

    Serial.println(
      "Azure MQTT is not connected."
    );

    if (!connectToAzure()) {

      rejectedCount++;

      Serial.println(
        "Message not published."
      );

      return;
    }
  }


  // ----------------------------------------------------------
  // Publish
  // ----------------------------------------------------------

  Serial.println();
  Serial.println(
    "Publishing to Azure..."
  );

  if (!gatewayControl.beforePublish(findSensorIndex(sourceId.c_str()), azurePayload.length())) {
    Serial.println("Not forwarded: approval, source, cadence or daily budget gate.");
    return;
  }

  bool published =
      mqttClient.publish(
        AZURE_MQTT_TOPIC,
        azurePayload.c_str()
      );


  gatewayControl.publishCompleted(published, findSensorIndex(sourceId.c_str()));

  if (published) {

    acceptedCount++;

    Serial.println();
    Serial.println(
      "================================"
    );

    Serial.println(
      " SENSOR → GATEWAY → AZURE PASS"
    );

    Serial.println(
      "================================"
    );

    Serial.println(
      "Sensor data received."
    );

    Serial.println(
      "Provenance validated."
    );

    Serial.println(
      "Gateway metadata added."
    );

    Serial.println(
      "Sensor data published to Azure."
    );

    Serial.print(
      "Accepted/published count: "
    );

    Serial.println(
      acceptedCount
    );

  } else {

    rejectedCount++;

    Serial.println();
    Serial.println(
      "================================"
    );

    Serial.println(
      " AZURE PUBLISH FAILED"
    );

    Serial.println(
      "================================"
    );

    Serial.print(
      "MQTT state: "
    );

    Serial.println(
      mqttClient.state()
    );
  }


  // ----------------------------------------------------------
  // Statistics
  // ----------------------------------------------------------

  Serial.println();

  Serial.println(
    "GATEWAY STATISTICS"
  );

  Serial.print(
    "Packets received: "
  );

  Serial.println(
    packetCount
  );

  Serial.print(
    "Published: "
  );

  Serial.println(
    acceptedCount
  );

  Serial.print(
    "Rejected: "
  );

  Serial.println(
    rejectedCount
  );

  Serial.print(
    "Replay candidates: "
  );

  Serial.println(
    replayCandidateCount
  );

  Serial.print(
    "Sequence gaps: "
  );

  Serial.println(
    sequenceGapCount
  );

  printSeparator();
}


// ============================================================
// CONNECT WIFI
// ============================================================

const char* wifiStatusLabel(int status) {
  switch (status) {
    case WL_CONNECTED:
      return "connected";
    case WL_NO_SSID_AVAIL:
      return "network not found";
    case WL_CONNECT_FAILED:
      return "connection/authentication failed";
    case WL_CONNECTION_LOST:
      return "connection lost";
    case WL_IDLE_STATUS:
      return "idle";
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return "connecting or unknown";
  }
}


void resetWiFiStation() {
  // A timed-out ESP32 connection can remain in CONNECTING state. Reset the
  // station before beginning a new attempt so WiFi.begin() is not rejected.
  prepareWiFiDiagnostics();
  WiFi.disconnect(false, false);
  delay(250);
  WiFi.mode(WIFI_AP);
  delay(250);
  WiFi.mode(WIFI_AP_STA);
  delay(250);
  WiFi.setSleep(false);
  scanConfiguredWiFi(WIFI_SSID);
}


void connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (!wifiRetryDue()) return;

  Serial.println();
  Serial.println(
    "Connecting to Wi-Fi..."
  );

  resetWiFiStation();

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  int attempts = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    attempts < 40
  ) {

    delay(500);

    Serial.print(".");

    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    reportWiFiSuccess();
    MDNS.end();
    if (MDNS.begin(GATEWAY_MDNS_NAME)) MDNS.addService("itp", "udp", GATEWAY_UDP_PORT);
    else Serial.println("mDNS unavailable; UDP broadcast discovery is still enabled.");
    Serial.print("Gateway Wi-Fi MAC (for optional DHCP reservation): ");
    Serial.println(WiFi.macAddress());
    // Rebind UDP after reconnect, including an initially offline startup.
    udp.stop();
    if (!udp.begin(UDP_LISTEN_PORT)) Serial.println("ERROR: UDP bind failed.");
    Serial.println(
      "Wi-Fi connected!"
    );

    Serial.print(
      "Gateway IP: "
    );

    Serial.println(
      WiFi.localIP()
    );

    Serial.print(
      "Signal strength: "
    );

    Serial.print(
      WiFi.RSSI()
    );

    Serial.println(
      " dBm"
    );

    printSeparator();

  } else {

    Serial.println(
      "Wi-Fi connection FAILED."
    );

    Serial.print(
      "Wi-Fi status: "
    );

    Serial.print(
      wifiStatusLabel(WiFi.status())
    );

    Serial.print(
      " ("
    );

    Serial.print(
      WiFi.status()
    );

    Serial.println(
      ")"
    );
    reportWiFiFailure();
  }
}


// ============================================================
// TIME SYNCHRONIZATION
// ============================================================

void synchronizeTime() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Skipping time sync: Wi-Fi is disconnected.");
    return;
  }
  static bool attempted = false;
  static unsigned long lastAttempt = 0;
  if (attempted && (unsigned long)(millis() - lastAttempt) < 30000) return;
  attempted = true;
  lastAttempt = millis();

  Serial.println();
  Serial.println(
    "Synchronizing time..."
  );

  configTime(
    GMT_OFFSET_SEC,
    DAYLIGHT_OFFSET_SEC,
    NTP_SERVER
  );

  struct tm timeinfo;

  int attempts = 0;

  while (
    !getLocalTime(&timeinfo, 10) &&
    attempts < 30
  ) {

    delay(500);

    Serial.print(".");

    attempts++;
  }

  Serial.println();

  if (getLocalTime(&timeinfo, 10)) {

    Serial.println(
      "Time synchronized."
    );

    Serial.print(
      "UTC time: "
    );

    Serial.println(
      getUtcTimestamp()
    );

    printSeparator();

  } else {

    Serial.println(
      "WARNING: Time synchronization failed."
    );
  }
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(2000);

  Serial.println();
  Serial.println();
  Serial.println(
    "################################"
  );

  Serial.println(
    "# Cross-Cloud Edge Trust"
  );

  Serial.println(
    "# Azure Gateway - XIAO ESP32-C3"
  );

  Serial.println(
    "# 3 Nodes: Approval + Daily Budget"
  );

  Serial.println(
    "################################"
  );


  // ----------------------------------------------------------
  // Wi-Fi
  // ----------------------------------------------------------

  WiFi.mode(WIFI_AP_STA);
  IPAddress sensorGateway;
  sensorGateway.fromString(SENSOR_GATEWAY_IPV4);
  if (strlen(SENSOR_WIFI_PASSWORD) < 12 || strlen(SENSOR_WIFI_PASSWORD) > 63 ||
      !WiFi.softAPConfig(sensorGateway, sensorGateway, IPAddress(255, 255, 255, 0)) ||
      !WiFi.softAP(SENSOR_WIFI_SSID, SENSOR_WIFI_PASSWORD, 1, false, SENSOR_NODE_COUNT)) {
    Serial.println("ERROR: private sensor Wi-Fi could not start; fix configuration before collection.");
    WiFi.softAPdisconnect(true);
    while (true) delay(1000);
  }
  Serial.print("Private sensor network gateway: ");
  Serial.println(WiFi.softAPIP());

  connectWiFi();
  gatewayControl.begin();


  // ----------------------------------------------------------
  // TLS
  // ----------------------------------------------------------

  secureClient.setCACert(
    AZURE_ROOT_CA
  );


  // ----------------------------------------------------------
  // MQTT
  // ----------------------------------------------------------

  mqttClient.setServer(
    AZURE_HUB_HOST,
    8883
  );


  // ----------------------------------------------------------
  // MQTT buffer
  // ----------------------------------------------------------

  mqttClient.setBufferSize(
    4096
  );
  mqttClient.setCallback([](char* topic, byte* payload, unsigned int length) {
    gatewayControl.onMessage(topic, payload, length);
  });


  // ----------------------------------------------------------
  // Time
  // ----------------------------------------------------------

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Network startup deferred: waiting for Wi-Fi before NTP and Azure.");
    return;
  }

  synchronizeTime();


  // ----------------------------------------------------------
  // UDP
  // ----------------------------------------------------------

  Serial.println(
    "Starting UDP listener..."
  );

  if (
    udp.begin(
      UDP_LISTEN_PORT
    )
  ) {

    Serial.println(
      "UDP listener started."
    );

    Serial.print(
      "Listening on port: "
    );

    Serial.println(
      UDP_LISTEN_PORT
    );

  } else {

    Serial.println(
      "ERROR: UDP initialization failed."
    );
  }


  // ----------------------------------------------------------
  // Azure
  // ----------------------------------------------------------

  connectToAzure();


  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "      AZURE GATEWAY READY"
  );

  Serial.println(
    "================================"
  );

  Serial.println(
    "Waiting for sensor data..."
  );

  printSeparator();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  // ----------------------------------------------------------
  // Maintain Wi-Fi
  // ----------------------------------------------------------

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    connectWiFi();

    delay(1000);

    return;
  }


  // Finish deferred clock initialization after an initially offline boot.
  if (time(nullptr) < 1700000000) {
    synchronizeTime();
    if (time(nullptr) < 1700000000) {
      delay(100);
      return;
    }
  }

  // ----------------------------------------------------------
  // Maintain MQTT
  // ----------------------------------------------------------

  if (
    !mqttClient.connected()
  ) {

    connectToAzure();
  }

  mqttClient.loop();
  gatewayControl.tick();


  // ----------------------------------------------------------
  // Receive sensor data
  // ----------------------------------------------------------

  receiveSensorData();


  delay(10);
}
