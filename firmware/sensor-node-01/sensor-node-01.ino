#include <WiFi.h>
#include <WiFiUdp.h>
#include <DHT.h>

#include "secrets.h"
#include "wifi_diagnostics.h"
#include "../shared/sensor_discovery.h"
#include <Preferences.h>


// ============================================================
// Sensor Node #1
// XIAO ESP32-C3 + DHT11
// ============================================================


// ============================================================
// Gateway Configuration
// ============================================================

// ITP Azure gateway private LAN address
const char* GATEWAY_IP = SENSOR_GATEWAY_IPV4;
IPAddress gatewayAddress;

// UDP intake port on the ITP Azure gateway
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
Preferences sensorSequenceStore;
bool sequenceStoreReady = false;


// ============================================================
// Reading Interval
// ============================================================

const unsigned long READING_INTERVAL =
    SENSOR_SAMPLING_INTERVAL_MS;

unsigned long lastReadingTime = 0;


// ============================================================
// Connect to Wi-Fi
// ============================================================

const char* wifiStatusLabel(int status)
{
    switch (status)
    {
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


void resetWiFiStation()
{
    // A timed-out ESP32 connection can remain in CONNECTING state. Reset the
    // station before beginning a new attempt so WiFi.begin() is not rejected.
    prepareWiFiDiagnostics();
    WiFi.disconnect(false, false);
    delay(250);
    WiFi.mode(WIFI_OFF);
    delay(250);
    WiFi.mode(WIFI_STA);
    delay(250);
    WiFi.setSleep(false);
    scanConfiguredWiFi(SENSOR_WIFI_SSID);
}


void connectWiFi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return;
    }

    if (!wifiRetryDue()) return;

    Serial.println();
    Serial.println("================================");
    Serial.println("       SENSOR NODE #1");
    Serial.println("================================");

    resetWiFiStation();

    WiFi.begin(
        SENSOR_WIFI_SSID,
        SENSOR_WIFI_PASSWORD
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
        reportWiFiSuccess();
        // Bind UDP even when the first connection only succeeds from loop().
        udp.stop();
        if (!udp.begin(5001)) Serial.println("ERROR: UDP bind failed.");
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
            "Gateway address: "
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

    Serial.println(
        "-------------------------------"
    );
}


// ============================================================
// Send Sensor Data
// ============================================================

void sendSensorData()
{
    if (!sequenceStoreReady || !discoverAzureGateway(udp, gatewayAddress))
    {
        Serial.println("Gateway discovery failed or sequence storage unavailable; no sample sent.");
        return;
    }
    Serial.print("Resolved gateway: ");
    Serial.println(gatewayAddress);
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

    if (sequenceNumber == UINT32_MAX) {
        Serial.println("Sequence counter exhausted; collection stopped.");
        sequenceStoreReady = false;
        return;
    }
    sequenceNumber++;
    if (sensorSequenceStore.putUInt("sequence", sequenceNumber) != sizeof(uint32_t)) {
        Serial.println("Sequence persistence failed; collection stopped.");
        sequenceStoreReady = false;
        return;
    }


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
        gatewayAddress,
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
    sequenceStoreReady = sensorSequenceStore.begin(SENSOR_ID, false);
    if (!sequenceStoreReady) {
        Serial.println("Cannot open persistent sensor counter; collection stopped.");
        return;
    }
    sequenceNumber = sensorSequenceStore.getUInt("sequence", 0);

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
    if (!sequenceStoreReady) { delay(1000); return; }
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
    // Send reading at the shared interval (40 seconds)
    // --------------------------------------------------------

    if (
        millis() - lastReadingTime >=
        READING_INTERVAL
    )
    {
        // Schedule from the attempt start to avoid accumulating discovery latency.
        lastReadingTime = millis();
        sendSensorData();
    }


    delay(
        50
    );
}
