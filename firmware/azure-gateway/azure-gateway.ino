#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <time.h>

#include "secrets.h"
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
// Sensor Node #1
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

const char* EXPECTED_SENSOR_ID =
    "sensor-node-01";


// ============================================================
// STATE
// ============================================================

unsigned long packetCount = 0;

unsigned long acceptedCount = 0;
unsigned long rejectedCount = 0;

unsigned long replayCandidateCount = 0;
unsigned long sequenceGapCount = 0;

unsigned long lastSensorSequence = 0;

bool hasReceivedSequence = false;


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

  bool sourceOk =
      extractStringField(
        payload,
        "source_id",
        sourceId
      );

  bool sequenceOk =
      extractUnsignedLongField(
        payload,
        "sequence",
        sequence
      );

  bool uptimeOk =
      extractUnsignedLongField(
        payload,
        "uptime_ms",
        uptimeMs
      );

  bool temperatureOk =
      extractFloatField(
        payload,
        "temperature",
        temperature
      );

  bool humidityOk =
      extractFloatField(
        payload,
        "humidity",
        humidity
      );


  // ----------------------------------------------------------
  // Required field validation
  // ----------------------------------------------------------

  if (
    !sourceOk ||
    !sequenceOk ||
    !uptimeOk ||
    !temperatureOk ||
    !humidityOk
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

  bool sourceValid =
      (sourceId == EXPECTED_SENSOR_ID);


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

  if (!hasReceivedSequence) {

    sequenceStatus =
        "FIRST";

  } else if (
    sequence <= lastSensorSequence
  ) {

    sequenceStatus =
        "REPLAY_OR_DUPLICATE";

  } else if (
    sequence > lastSensorSequence + 1
  ) {

    sequenceStatus =
        "SEQUENCE_GAP";

  } else {

    sequenceStatus =
        "NORMAL";
  }


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

  Serial.print("Expected source: ");
  Serial.println(EXPECTED_SENSOR_ID);

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

  if (
    !hasReceivedSequence ||
    sequence > lastSensorSequence
  ) {

    lastSensorSequence =
        sequence;

    hasReceivedSequence =
        true;
  }


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

  bool published =
      mqttClient.publish(
        AZURE_MQTT_TOPIC,
        azurePayload.c_str()
      );


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

void connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.println();
  Serial.println(
    "Connecting to Wi-Fi..."
  );

  WiFi.mode(WIFI_STA);

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
  }
}


// ============================================================
// TIME SYNCHRONIZATION
// ============================================================

void synchronizeTime() {

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
    !getLocalTime(&timeinfo) &&
    attempts < 30
  ) {

    delay(500);

    Serial.print(".");

    attempts++;
  }

  Serial.println();

  if (getLocalTime(&timeinfo)) {

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
    "# Phase 2: Provenance + Validation"
  );

  Serial.println(
    "################################"
  );


  // ----------------------------------------------------------
  // Wi-Fi
  // ----------------------------------------------------------

  connectWiFi();


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
    1024
  );


  // ----------------------------------------------------------
  // Time
  // ----------------------------------------------------------

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


  // ----------------------------------------------------------
  // Maintain MQTT
  // ----------------------------------------------------------

  if (
    !mqttClient.connected()
  ) {

    connectToAzure();
  }

  mqttClient.loop();


  // ----------------------------------------------------------
  // Receive sensor data
  // ----------------------------------------------------------

  receiveSensorData();


  delay(10);
}