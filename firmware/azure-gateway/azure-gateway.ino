#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

#include <time.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>

#include "secrets.h"
#include "azure_root_ca.h"


// ============================================================
// Azure MQTT Configuration
// ============================================================

const int MQTT_PORT = 8883;

const char* MQTT_USERNAME =
    "iot-hub-crosscloud.azure-devices.net/azure-gateway-01/?api-version=2021-04-12";

const char* MQTT_TOPIC =
    "devices/azure-gateway-01/messages/events/";


// ============================================================
// Local Sensor Gateway Configuration
// ============================================================

const unsigned int UDP_PORT = 5000;

const char* GATEWAY_ID =
    "azure-gateway-01";


// ============================================================
// Network Clients
// ============================================================

WiFiClientSecure secureClient;

PubSubClient mqttClient(
    secureClient
);

WiFiUDP udp;


// ============================================================
// Sensor Tracking
// ============================================================

unsigned long receivedPackets = 0;

unsigned long lastSensorSequence = 0;

unsigned long lastSensorPacketTime = 0;


// ============================================================
// URL Encode
// ============================================================

String urlEncode(
    const String& input
)
{
    const char* hex =
        "0123456789ABCDEF";

    String output;

    for (
        size_t i = 0;
        i < input.length();
        i++
    )
    {
        char c = input[i];

        if (
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '_' ||
            c == '.' ||
            c == '~'
        )
        {
            output += c;
        }
        else
        {
            output += '%';

            output += hex[
                (c >> 4) & 0x0F
            ];

            output += hex[
                c & 0x0F
            ];
        }
    }

    return output;
}


// ============================================================
// Generate Azure SAS Token
// ============================================================

String generateSasToken()
{
    const long TOKEN_LIFETIME = 3600;

    time_t now =
        time(nullptr);

    if (
        now < 100000
    )
    {
        Serial.println(
            "ERROR: System time is not synchronized."
        );

        return "";
    }

    time_t expiry =
        now + TOKEN_LIFETIME;


    // --------------------------------------------------------
    // Resource URI
    // --------------------------------------------------------

    String resourceUri =
        String(AZURE_HUB_HOST) +
        "/devices/" +
        AZURE_DEVICE_ID;


    String encodedResourceUri =
        urlEncode(
            resourceUri
        );


    // --------------------------------------------------------
    // String to sign
    // --------------------------------------------------------

    String stringToSign =
        encodedResourceUri +
        "\n" +
        String(
            (long)expiry
        );


    // --------------------------------------------------------
    // Decode device key
    // --------------------------------------------------------

    size_t keyLength = 0;

    unsigned char decodedKey[64];

    int result =
        mbedtls_base64_decode(
            decodedKey,
            sizeof(decodedKey),
            &keyLength,
            (const unsigned char*)
                AZURE_DEVICE_KEY,
            strlen(
                AZURE_DEVICE_KEY
            )
        );

    if (
        result != 0
    )
    {
        Serial.println(
            "ERROR: Failed to decode Azure device key."
        );

        return "";
    }


    // --------------------------------------------------------
    // HMAC-SHA256
    // --------------------------------------------------------

    unsigned char hmacResult[32];

    const mbedtls_md_info_t* mdInfo =
        mbedtls_md_info_from_type(
            MBEDTLS_MD_SHA256
        );

    if (
        mdInfo == nullptr
    )
    {
        Serial.println(
            "ERROR: SHA256 unavailable."
        );

        return "";
    }


    mbedtls_md_context_t ctx;

    mbedtls_md_init(
        &ctx
    );


    result =
        mbedtls_md_setup(
            &ctx,
            mdInfo,
            1
        );

    if (
        result != 0
    )
    {
        mbedtls_md_free(
            &ctx
        );

        return "";
    }


    result =
        mbedtls_md_hmac_starts(
            &ctx,
            decodedKey,
            keyLength
        );

    if (
        result != 0
    )
    {
        mbedtls_md_free(
            &ctx
        );

        return "";
    }


    result =
        mbedtls_md_hmac_update(
            &ctx,
            (const unsigned char*)
                stringToSign.c_str(),
            stringToSign.length()
        );

    if (
        result != 0
    )
    {
        mbedtls_md_free(
            &ctx
        );

        return "";
    }


    result =
        mbedtls_md_hmac_finish(
            &ctx,
            hmacResult
        );

    mbedtls_md_free(
        &ctx
    );


    if (
        result != 0
    )
    {
        return "";
    }


    // --------------------------------------------------------
    // Base64 encode signature
    // --------------------------------------------------------

    unsigned char encodedSignature[64];

    size_t encodedLength = 0;

    result =
        mbedtls_base64_encode(
            encodedSignature,
            sizeof(encodedSignature) - 1,
            &encodedLength,
            hmacResult,
            sizeof(hmacResult)
        );

    if (
        result != 0
    )
    {
        return "";
    }

    encodedSignature[
        encodedLength
    ] = '\0';


    // --------------------------------------------------------
    // URL encode signature
    // --------------------------------------------------------

    String signature =
        urlEncode(
            String(
                (char*)encodedSignature
            )
        );


    // --------------------------------------------------------
    // Construct SAS token
    // --------------------------------------------------------

    String token =
        "SharedAccessSignature sr=" +
        encodedResourceUri +
        "&sig=" +
        signature +
        "&se=" +
        String(
            (long)expiry
        );


    return token;
}


// ============================================================
// Connect Wi-Fi
// ============================================================

void connectWiFi()
{
    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "     AZURE GATEWAY ESP32-C3"
    );

    Serial.println(
        "================================"
    );


    WiFi.mode(
        WIFI_STA
    );


    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );


    Serial.print(
        "Connecting to Wi-Fi"
    );


    int attempts = 0;


    while (
        WiFi.status() != WL_CONNECTED &&
        attempts < 30
    )
    {
        delay(500);

        Serial.print(
            "."
        );

        attempts++;
    }


    Serial.println();


    if (
        WiFi.status() == WL_CONNECTED
    )
    {
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
    }
    else
    {
        Serial.println(
            "Wi-Fi connection FAILED."
        );
    }


    Serial.println(
        "-------------------------------"
    );
}


// ============================================================
// Synchronize Time
// ============================================================

bool synchronizeTime()
{
    Serial.println();
    Serial.println(
        "Synchronizing time..."
    );


    configTime(
        0,
        0,
        "pool.ntp.org",
        "time.nist.gov"
    );


    time_t now =
        time(nullptr);


    int attempts = 0;


    while (
        now < 100000 &&
        attempts < 30
    )
    {
        delay(500);

        Serial.print(
            "."
        );

        now =
            time(nullptr);

        attempts++;
    }


    Serial.println();


    if (
        now > 100000
    )
    {
        Serial.println(
            "Time synchronized."
        );


        struct tm timeinfo;


        gmtime_r(
            &now,
            &timeinfo
        );


        Serial.printf(
            "UTC time: %04d-%02d-%02d %02d:%02d:%02d\n",
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec
        );


        Serial.println(
            "-------------------------------"
        );


        return true;
    }


    Serial.println(
        "WARNING: Time synchronization failed."
    );


    return false;
}


// ============================================================
// MQTT Callback
// ============================================================

void mqttCallback(
    char* topic,
    byte* payload,
    unsigned int length
)
{
    Serial.println();
    Serial.println(
        "MQTT message received."
    );

    Serial.print(
        "Topic: "
    );

    Serial.println(
        topic
    );
}


// ============================================================
// Connect Azure MQTT
// ============================================================

bool connectAzure()
{
    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "   CONNECTING TO AZURE MQTT"
    );

    Serial.println(
        "================================"
    );


    // --------------------------------------------------------
    // TLS
    // --------------------------------------------------------

    secureClient.setCACert(
        AZURE_ROOT_CA
    );

    secureClient.setTimeout(
        10000
    );


    // --------------------------------------------------------
    // MQTT
    // --------------------------------------------------------

    mqttClient.setServer(
        AZURE_HUB_HOST,
        MQTT_PORT
    );

    mqttClient.setCallback(
        mqttCallback
    );

    mqttClient.setKeepAlive(
        60
    );

    mqttClient.setBufferSize(
        1024
    );


    // --------------------------------------------------------
    // SAS
    // --------------------------------------------------------

    Serial.println();

    Serial.println(
        "Generating SAS token..."
    );


    String sasToken =
        generateSasToken();


    if (
        sasToken.length() == 0
    )
    {
        Serial.println(
            "SAS token generation FAILED."
        );

        return false;
    }


    Serial.println(
        "SAS token generated."
    );


    // --------------------------------------------------------
    // MQTT CONNECT
    // --------------------------------------------------------

    Serial.println();

    Serial.println(
        "Connecting MQTT..."
    );


    Serial.print(
        "Host: "
    );

    Serial.println(
        AZURE_HUB_HOST
    );


    Serial.print(
        "Port: "
    );

    Serial.println(
        MQTT_PORT
    );


    Serial.print(
        "Device ID: "
    );

    Serial.println(
        AZURE_DEVICE_ID
    );


    bool connected =
        mqttClient.connect(
            AZURE_DEVICE_ID,
            MQTT_USERNAME,
            sasToken.c_str()
        );


    if (
        connected
    )
    {
        Serial.println();
        Serial.println(
            "================================"
        );

        Serial.println(
            "   AZURE MQTT CONNECTED!"
        );

        Serial.println(
            "================================"
        );


        Serial.println();
        Serial.println(
            "Device authentication: PASS"
        );

        Serial.println(
            "MQTT connection: PASS"
        );


        return true;
    }


    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "   AZURE MQTT CONNECTION FAILED"
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


    return false;
}


// ============================================================
// Start UDP Sensor Receiver
// ============================================================

void startUDP()
{
    udp.begin(
        UDP_PORT
    );


    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "      SENSOR UDP RECEIVER"
    );

    Serial.println(
        "================================"
    );


    Serial.print(
        "Listening on UDP port: "
    );

    Serial.println(
        UDP_PORT
    );


    Serial.print(
        "Gateway IP: "
    );

    Serial.println(
        WiFi.localIP()
    );


    Serial.println(
        "Waiting for Sensor Node #1..."
    );


    Serial.println(
        "-------------------------------"
    );
}


// ============================================================
// Get Current UTC Timestamp
// ============================================================

String getTimestamp()
{
    time_t now =
        time(nullptr);


    struct tm timeinfo;


    gmtime_r(
        &now,
        &timeinfo
    );


    char buffer[32];


    snprintf(
        buffer,
        sizeof(buffer),
        "%04d-%02d-%02dT%02d:%02d:%02dZ",
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    );


    return String(
        buffer
    );
}


// ============================================================
// Receive Sensor Packet
// ============================================================

void receiveSensorData()
{
    int packetSize =
        udp.parsePacket();


    if (
        packetSize <= 0
    )
    {
        return;
    }


    // --------------------------------------------------------
    // Read UDP packet
    // --------------------------------------------------------

    char packetBuffer[512];


    int length =
        udp.read(
            packetBuffer,
            sizeof(packetBuffer) - 1
        );


    if (
        length <= 0
    )
    {
        return;
    }


    packetBuffer[length] =
        '\0';


    String sensorPayload =
        String(
            packetBuffer
        );


    // --------------------------------------------------------
    // Packet information
    // --------------------------------------------------------

    IPAddress senderIP =
        udp.remoteIP();


    unsigned int senderPort =
        udp.remotePort();


    receivedPackets++;

    lastSensorPacketTime =
        millis();


    // --------------------------------------------------------
    // Display received data
    // --------------------------------------------------------

    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "    SENSOR DATA RECEIVED"
    );

    Serial.println(
        "================================"
    );


    Serial.print(
        "Source IP: "
    );

    Serial.println(
        senderIP
    );


    Serial.print(
        "Source port: "
    );

    Serial.println(
        senderPort
    );


    Serial.print(
        "Packet number: "
    );

    Serial.println(
        receivedPackets
    );


    Serial.print(
        "Sensor payload: "
    );

    Serial.println(
        sensorPayload
    );


    // --------------------------------------------------------
    // Basic sequence extraction
    // --------------------------------------------------------

    int sequencePosition =
        sensorPayload.indexOf(
            "\"sequence\":"
        );


    if (
        sequencePosition >= 0
    )
    {
        int valueStart =
            sequencePosition +
            11;


        int valueEnd =
            sensorPayload.indexOf(
                ',',
                valueStart
            );


        if (
            valueEnd < 0
        )
        {
            valueEnd =
                sensorPayload.indexOf(
                    '}',
                    valueStart
                );
        }


        if (
            valueEnd > valueStart
        )
        {
            String sequenceText =
                sensorPayload.substring(
                    valueStart,
                    valueEnd
                );


            unsigned long sequence =
                sequenceText.toInt();


            Serial.print(
                "Sequence number: "
            );

            Serial.println(
                sequence
            );


            // ------------------------------------------------
            // Detect duplicate/out-of-order sequence
            // ------------------------------------------------

            if (
                sequence <=
                lastSensorSequence
            )
            {
                Serial.println();
                Serial.println(
                    "WARNING: DUPLICATE OR"
                );

                Serial.println(
                    "OUT-OF-ORDER SEQUENCE!"
                );

                Serial.print(
                    "Previous: "
                );

                Serial.println(
                    lastSensorSequence
                );

                Serial.print(
                    "Current: "
                );

                Serial.println(
                    sequence
                );
            }


            lastSensorSequence =
                sequence;
        }
    }


    // --------------------------------------------------------
    // Build gateway-enriched telemetry
    // --------------------------------------------------------

    String gatewayTimestamp =
        getTimestamp();


    String finalPayload = "{";

    finalPayload +=
        "\"gateway_id\":\"";

    finalPayload +=
        GATEWAY_ID;

    finalPayload += "\",";


    finalPayload +=
        "\"gateway_timestamp\":\"";

    finalPayload +=
        gatewayTimestamp;

    finalPayload += "\",";


    finalPayload +=
        "\"source_ip\":\"";

    finalPayload +=
        senderIP.toString();

    finalPayload += "\",";


    finalPayload +=
        "\"sensor_data\":";

    finalPayload +=
        sensorPayload;


    finalPayload += "}";


    // --------------------------------------------------------
    // Display final telemetry
    // --------------------------------------------------------

    Serial.println();
    Serial.println(
        "Gateway-enriched payload:"
    );

    Serial.println(
        finalPayload
    );


    // --------------------------------------------------------
    // Publish to Azure
    // --------------------------------------------------------

    if (
        mqttClient.connected()
    )
    {
        bool published =
            mqttClient.publish(
                MQTT_TOPIC,
                finalPayload.c_str(),
                false
            );


        Serial.println();


        if (
            published
        )
        {
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
                "Sensor data accepted by gateway."
            );

            Serial.println(
                "Sensor data published to Azure."
            );
        }
        else
        {
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
    }
    else
    {
        Serial.println();
        Serial.println(
            "WARNING: Azure MQTT is disconnected."
        );
    }


    Serial.println(
        "-------------------------------"
    );
}


// ============================================================
// Setup
// ============================================================

void setup()
{
    Serial.begin(
        115200
    );


    delay(
        1000
    );


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
        "################################"
    );


    // --------------------------------------------------------
    // Wi-Fi
    // --------------------------------------------------------

    connectWiFi();


    if (
        WiFi.status() != WL_CONNECTED
    )
    {
        Serial.println(
            "Stopping because Wi-Fi failed."
        );

        return;
    }


    // --------------------------------------------------------
    // NTP
    // --------------------------------------------------------

    if (
        !synchronizeTime()
    )
    {
        Serial.println(
            "Stopping because time sync failed."
        );

        return;
    }


    // --------------------------------------------------------
    // Azure MQTT
    // --------------------------------------------------------

    if (
        !connectAzure()
    )
    {
        Serial.println();
        Serial.println(
            "Azure MQTT connection failed."
        );

        return;
    }


    // --------------------------------------------------------
    // UDP Sensor Receiver
    // --------------------------------------------------------

    startUDP();


    Serial.println();
    Serial.println(
        "SYSTEM READY."
    );

    Serial.println(
        "Waiting for Sensor Node #1..."
    );
}


// ============================================================
// Loop
// ============================================================

void loop()
{
    // --------------------------------------------------------
    // Wi-Fi
    // --------------------------------------------------------

    if (
        WiFi.status() != WL_CONNECTED
    )
    {
        Serial.println(
            "Wi-Fi disconnected."
        );

        connectWiFi();

        delay(
            1000
        );

        return;
    }


    // --------------------------------------------------------
    // Azure MQTT
    // --------------------------------------------------------

    if (
        !mqttClient.connected()
    )
    {
        Serial.println(
            "Azure MQTT disconnected."
        );

        if (
            !connectAzure()
        )
        {
            delay(
                5000
            );

            return;
        }
    }


    // --------------------------------------------------------
    // MQTT processing
    // --------------------------------------------------------

    mqttClient.loop();


    // --------------------------------------------------------
    // Check for Sensor Node #1
    // --------------------------------------------------------

    receiveSensorData();


    delay(
        10
    );
}