# ITP final code handoff - 30 October 2026

This is the final code handoff for the current Azure collection architecture,
not a claim that all research milestones or physical acceptance tests are complete.
Firmware upload is left to Ahmed. No board is flashed by this handoff.

## Four sketches

| Board | Sketch | Sensor |
|---|---|---|
| Azure gateway | firmware/azure-gateway/azure-gateway.ino | No DHT |
| Sensor 1 | firmware/sensor-node-01/sensor-node-01.ino | DHT11, D0 |
| Sensor 2 | firmware/sensor-node-02/sensor-node-02.ino | DHT11, D0 |
| Sensor 3 | firmware/sensor-node-03/sensor-node-03.ino | DHT22, D0 |

All boards use Seeed XIAO ESP32-C3, Arduino FQBN esp32:esp32:XIAO_ESP32C3,
and Serial Monitor at 115200 baud. Keep the complete firmware folder together.
Verified build environment: ESP32 core 3.3.12, ArduinoJson 7.4.3,
PubSubClient 2.8, DHT sensor library 1.4.7, Adafruit Unified Sensor 1.1.15.

## Credentials and network

Existing local secrets files are ready and must not be committed.
For a fresh clone, copy each secrets.example.h to secrets.h in the same folder:
shared, azure-gateway, sensor-node-01, sensor-node-02 and sensor-node-03.
Fill in the shared Wi-Fi values and gateway Azure device key locally.
The sensor files only include the shared header.

- Gateway uplink: WIFI_SSID and WIFI_PASSWORD in firmware/shared/secrets.h.
- Private LAN: ITP-Sensors; use the SAME private password on all four boards.
- Gateway private address: 192.168.77.1; UDP port: 5000.
- Sensors join the gateway LAN, not the home router or hotspot directly.
- Uplink credentials change: reflash gateway only. Private LAN name/password
  change: reflash ALL FOUR. This release renames the private LAN to ITP-Sensors,
  so all three sensors also need the new firmware.
- Keep the hotspot in 2.4 GHz compatibility mode and antennas firmly attached.
- Reported firmware: itp-azure-3nodes-v1; optional mDNS name: itp-azure.

## Upload and verify

1. Connect only the intended board, select XIAO_ESP32C3 and its actual COM port.
2. Open its matching sketch, verify, then upload. Do not erase all flash/NVS:
   the persistent sequence and daily telemetry budget must be retained.
3. Flash gateway first, then Sensors 1, 2 and 3 using their matching sketches.
4. Open Serial Monitor at 115200. Check gateway AP, uplink, time and MQTT startup.
5. Confirm each sensor's identity and private-LAN connection.
6. In Azure Portal, open iot-hub-crosscloud -> azure-gateway-01 -> Device twin.
   Copy the CURRENT reported collection.requestId into desired collection.requestId,
   set desired collection.enabled=true, and give expiresAtUtc a future UTC time
   no more than seven days ahead. Do not reuse a previous boot request.
7. Verify reported state collecting, publishing from all three sources, and new
   data in crosscloudresearch01/research-raw after routing/batching delay.
8. Check revoke, expiry and a fresh startup's default-denied state before a trial.

Sampling is every 40 seconds: approximately 6,480 readings/day across all three.
The gateway limits telemetry to 7,000 units per UTC day. Twin/control traffic also
consumes hub quota. Approval gates forwarding after the MQTT control connection;
it does not prevent sensors measuring locally. MQTT/TLS uses SAS authentication.
There is no durable offline queue or signed UDP sensor identity in this release.

## Checks

The two native C++ tests cover approval/default deny/stale boot/expiry/revocation,
quota/cadence boundaries, and independent three-source sequence handling.
Watch-GatewayApproval.ps1 -SelfTest checks popup request filtering.
All four sketches passed Arduino compilation. See cloud/azure/final-code-verification.json for build sizes and source hashes.
Hardware upload, new boot approval and live storage delivery remain the next
physical checks; compilation alone does not prove those outcomes.

For detailed operation, see azure-operations.md. AWS scaffolding is outside
current scope. Data-science analysis remains a separate workstream.
