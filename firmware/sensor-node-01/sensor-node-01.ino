#include <WiFi.h>
#include <WiFiUdp.h>
#include <DHT.h>

#include "secrets.h"


// ============================================================
// Sensor Node #1
// XIAO ESP32-C3 + DHT11
// ============================================================


// ============================================================
// Gateway Configuration
// ============================================================

// XIAO #3 Azure Gateway IP
const char* GATEWAY_IP = "192.168.100.63";

// UDP port used by XIAO #3
const unsigned int GATEWAY_PORT = 5000;


// ============================================================
// DHT11 Configuration
// ============================================================

#define DHTPIN D0
#define DHTTYPE DHT11

DHT dht(
    DHTPIN,
    DHTTYPE
);


// ============================================================
// UDP
// ============================================================

WiFiUDP udp;


// ============================================================
// Sensor Identity
// ============================================================

const char* SENSOR_ID =
    "sensor-node-01";


// ============================================================
// Sequence Number
// ============================================================
//
// This will later help us detect:
// - duplicate messages
// - replayed messages
// - missing messages
// - out-of-order messages
//
// ============================================================

unsigned long sequenceNumber = 0;


// ============================================================
// Reading Interval
// ============================================================

const unsigned long READING_INTERVAL =
    5000;

unsigned long lastReadingTime = 0;


// ============================================================
// Connect to Wi-Fi
// ============================================================

void connectWiFi()
{
    Serial.println();
    Serial.println("================================");
    Serial.println("       SENSOR NODE #1");
    Serial.println("================================");

    WiFi.mode(WIFI_STA);

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

        Serial.print(".");

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
            "Sensor IP: "
        );

        Serial.println(
            WiFi.localIP()
        );

        Serial.print(
            "Gateway IP: "
        );

        Serial.println(
            GATEWAY_IP
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
// Send Sensor Data
// ============================================================

void sendSensorData()
{
    // --------------------------------------------------------
    // Read DHT11
    // --------------------------------------------------------

    float humidity =
        dht.readHumidity();

    float temperature =
        dht.readTemperature();


    // --------------------------------------------------------
    // Validate sensor reading
    // --------------------------------------------------------

    if (
        isnan(humidity) ||
        isnan(temperature)
    )
    {
        Serial.println();
        Serial.println(
            "ERROR: Failed to read DHT11!"
        );

        return;
    }


    // --------------------------------------------------------
    // Increment sequence number
    // --------------------------------------------------------

    sequenceNumber++;


    // --------------------------------------------------------
    // Sensor uptime
    // --------------------------------------------------------

    unsigned long uptime =
        millis();


    // --------------------------------------------------------
    // Build JSON payload
    // --------------------------------------------------------

    String payload = "{";

    payload +=
        "\"source_id\":\"";

    payload +=
        SENSOR_ID;

    payload +=
        "\",";


    payload +=
        "\"sequence\":";

    payload +=
        sequenceNumber;

    payload +=
        ",";


    payload +=
        "\"uptime_ms\":";

    payload +=
        uptime;

    payload +=
        ",";


    payload +=
        "\"temperature\":";

    payload +=
        String(
            temperature,
            2
        );

    payload +=
        ",";


    payload +=
        "\"humidity\":";

    payload +=
        String(
            humidity,
            2
        );


    payload +=
        "}";


    // --------------------------------------------------------
    // Send UDP packet
    // --------------------------------------------------------

    udp.beginPacket(
        GATEWAY_IP,
        GATEWAY_PORT
    );

    udp.print(
        payload
    );

    int result =
        udp.endPacket();


    // --------------------------------------------------------
    // Display sensor information
    // --------------------------------------------------------

    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "      SENSOR READING"
    );

    Serial.println(
        "================================"
    );


    Serial.print(
        "Sensor ID: "
    );

    Serial.println(
        SENSOR_ID
    );


    Serial.print(
        "Temperature: "
    );

    Serial.print(
        temperature
    );

    Serial.println(
        " °C"
    );


    Serial.print(
        "Humidity: "
    );

    Serial.print(
        humidity
    );

    Serial.println(
        " %"
    );


    Serial.print(
        "Sequence: "
    );

    Serial.println(
        sequenceNumber
    );


    Serial.print(
        "Uptime: "
    );

    Serial.print(
        uptime
    );

    Serial.println(
        " ms"
    );


    // --------------------------------------------------------
    // UDP result
    // --------------------------------------------------------

    Serial.print(
        "UDP destination: "
    );

    Serial.print(
        GATEWAY_IP
    );

    Serial.print(
        ":"
    );

    Serial.println(
        GATEWAY_PORT
    );


    Serial.print(
        "UDP send: "
    );


    if (
        result == 1
    )
    {
        Serial.println(
            "SUCCESS"
        );
    }
    else
    {
        Serial.println(
            "FAILED"
        );
    }


    // --------------------------------------------------------
    // Display complete payload
    // --------------------------------------------------------

    Serial.print(
        "Payload: "
    );

    Serial.println(
        payload
    );


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
        "# Sensor Node #1"
    );

    Serial.println(
        "# XIAO ESP32-C3 + DHT11"
    );

    Serial.println(
        "################################"
    );


    // --------------------------------------------------------
    // Start DHT11
    // --------------------------------------------------------

    Serial.println();

    Serial.println(
        "Starting DHT11..."
    );

    dht.begin();

    delay(
        1000
    );

    Serial.println(
        "DHT11 initialized."
    );


    // --------------------------------------------------------
    // Connect Wi-Fi
    // --------------------------------------------------------

    connectWiFi();


    if (
        WiFi.status() != WL_CONNECTED
    )
    {
        Serial.println();

        Serial.println(
            "Stopping because Wi-Fi failed."
        );

        return;
    }


    // --------------------------------------------------------
    // Start UDP
    // --------------------------------------------------------

    udp.begin(
        5001
    );


    Serial.println();

    Serial.println(
        "UDP initialized."
    );


    Serial.print(
        "Sending sensor data to: "
    );

    Serial.print(
        GATEWAY_IP
    );

    Serial.print(
        ":"
    );

    Serial.println(
        GATEWAY_PORT
    );


    Serial.println(
        "-------------------------------"
    );


    // --------------------------------------------------------
    // Send first reading
    // --------------------------------------------------------

    sendSensorData();


    lastReadingTime =
        millis();
}


// ============================================================
// Main Loop
// ============================================================

void loop()
{
    // --------------------------------------------------------
    // Check Wi-Fi
    // --------------------------------------------------------

    if (
        WiFi.status() != WL_CONNECTED
    )
    {
        Serial.println();

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
    // Send reading every 5 seconds
    // --------------------------------------------------------

    if (
        millis() - lastReadingTime >=
        READING_INTERVAL
    )
    {
        sendSensorData();

        lastReadingTime =
            millis();
    }


    delay(
        50
    );
}