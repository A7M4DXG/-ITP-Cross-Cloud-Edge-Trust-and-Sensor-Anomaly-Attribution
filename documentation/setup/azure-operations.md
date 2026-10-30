# Azure operations: one gateway and three sensors

This document supersedes older two-node planning notes. Primary firmware is in
this repository. The separate data-science repository's azure-integration
firmware is a historical snapshot; do not flash that snapshot for this setup.

## Current status

Cloud configuration is deployed: diagnostics, dataset access and default-denied
approval properties. On 28 October Malaysia time, the gateway reported the new
firmware and collecting state, and all three source IDs were decoded from new
raw blobs. This establishes delivery for the checked window, not sensor accuracy
or an uninterrupted multi-day run. A later check verified corrected Node 3
readings near 29.8 C in Azure Storage; Node 2's high humidity still needs a
physical/reference check.

Existing telemetry format remains schema 1.0 and the existing route/container
are preserved. Hashem owns his adapters and analysis; no replacement ingestion
application was created.

## Selected flow

Three XIAO sensor nodes -> private sensor Wi-Fi -> Azure XIAO gateway ->
home Wi-Fi or phone hotspot -> MQTT/TLS/SAS -> Azure IoT Hub -> raw Blob Storage
-> Hashem's existing readers/adapters.

Azure Monitor diagnostics provide operational evidence separately. Device twin
desired properties provide human permission to forward readings after startup.

Nodes 1-2 are blue DHT11 sensors. Node 3 is white, expected DHT22/AM2302; confirm
the marking. Its original DHT11 setting produced about 2.1-2.2 C while the other
nodes measured about 30 C. The Node 3 sketch now selects DHT22. A subsequent
Serial log shows plausible values from 2026-10-27T16:22:35Z; a corrected Node 3
record at 2026-10-27T16:23:15Z was also decoded from Azure Storage. Preserve
earlier readings as setup data; do not rewrite them.
Record the actual model and upload/trial boundary for each node. A mixed-model
comparison is different from comparing three identical sensors. Gateway NORMAL
is a range/format result, not calibration or cross-sensor agreement evidence.

See [the architecture source](../../diagrams/azure-three-sensor-flow.mmd).

## One shared Wi-Fi configuration

Edit ONLY firmware/shared/secrets.h for Wi-Fi credentials. This real credential
file is ignored by Git. Its example is secrets.example.h.

- WIFI_SSID / WIFI_PASSWORD: gateway internet uplink (home Wi-Fi or phone hotspot).
- SENSOR_WIFI_SSID / SENSOR_WIFI_PASSWORD: private sensor network, generated with
  a strong local PSK. Keep these stable when changing the internet uplink.
- Sensors use the private network; the gateway simultaneously operates AP + STA.
- The gateway's sensor-facing address is always 192.168.77.1/24.
- Its uplink gets a DHCP address. No home-router static address or public Azure
  static IP is required.
- The uplink must support 2.4 GHz. Enable that band/compatibility mode on a phone
  hotspot. The private sensor LAN must not overlap the uplink subnet; if a custom
  router ever uses 192.168.77.0/24, change the shared sensor-LAN address and reflash
  all four once. The currently observed home network is a different subnet.
- Sensor DHCP addresses can vary. Map actual source IPs to source IDs for each
  trial; never assume Node 1 always receives the first lease.
- Changing home/hotspot uplink credentials requires reflashing ONLY the gateway.
  Changing the private sensor network credentials requires reflashing all four.
  Editing a local header never updates an already-flashed board automatically.
- Keep the entire firmware directory together when opening sketches: they
  include shared headers from the sibling shared directory.
- Sensor-network clients do not require internet access or NAT through the
  gateway. They only send UDP to the gateway itself.
- Your PC stays on home Wi-Fi/hotspot for Azure Portal and Arduino IDE; it does
  not need to join the private sensor network.
- AP/STA share one radio/channel. Initial uplink scans and moving networks can
  interrupt local association; record those intervals in reliability trials.
- Wi-Fi link protection is not message-level sensor authentication. This remains
  UDP with claimed source IDs, not signed sensor telemetry.

The ignored gateway firmware/azure-gateway/secrets.h still holds only the Azure
host/device/key configuration plus an include of the shared Wi-Fi header.
The migration preserved all existing Azure values without printing them.

## Cadence and budget

All three nodes share SENSOR_SAMPLING_INTERVAL_MS = 40000 in
firmware/shared/collection_config.h.

- Approximately 2,160 readings per node per full day, 6,480 total.
- This treats the user's 7,000/day requirement as a TOTAL upper limit, not an
  exact target and not 7,000 per node.
- Each accepted telemetry body is limited to 512 bytes for the F1 one-unit
  contract. The inspected older messages were 355-369 bytes.
- The gateway conservatively reserves telemetry quota before publishing and
  persists its count in NVS. Failed sends are not refunded.
- The telemetry hard ceiling is 7,000 units per UTC day across all sources,
  including across ordinary gateway power cycles.
- Only the three expected source IDs are admitted to the reliability dataset.
  Per-source forwarding has a 38-second floor to tolerate jitter around the
  planned 40-second sampling interval; repeated/replayed input cannot create a
  higher regular output rate for one continuously running gateway.
- Twin/control operations ALSO consume F1 quota and are not part of that local
  telemetry ledger. In the normal 6,480-event plan there is headroom below the
  hub's 8,000 daily quota. Excessive restarts or repeated Portal/twin queries can
  still consume this headroom. Do not treat the ledger as a measurement of all
  IoT Hub billable operations.
- Status reports are normally limited to every 15 minutes plus state changes
  and the first accepted publish for each source. Twin synchronization retries
  are bounded. Avoid continuously refreshing the complete twin.
- UTC-day reset is midnight UTC / 08:00 Malaysia. Record partial first/last days
  and use consistent observation windows in the comparison.
- Each sensor persists its sequence counter before sending, so a normal sensor
  power cycle does not restart its sequence at one.
- Flash/NVS storage failures stop collection rather than silently reset budget
  or counters. Ordinary firmware uploads preserve NVS unless flash is erased.
  Erasing/replacing the gateway flash can reset its local budget; check the
  actual cloud quota before doing that.
- Wi-Fi/MQTT outages, approval gaps and DHT read failures can still cause missing
  observations. This is not a durable store-and-forward system. PubSubClient's
  successful publish result is local acceptance, not proof of Blob delivery.

## Human approval in the Azure Portal

Initial authentication is still the gateway's device SAS credential over TLS.
The human gate authorizes collection/forwarding AFTER that control connection
exists. It does not hold the MQTT connection itself pending approval.

Every gateway power cycle generates a new requestId. The new firmware defaults
to denied. An old desired enabled=true alone cannot authorize a different boot.

1. Open Azure Portal -> iot-hub-crosscloud -> Devices -> azure-gateway-01 ->
   Device twin.
2. Wait for properties.reported.firmware = itp-azure-3nodes-v1 and
   properties.reported.collection.state = awaiting_approval.
3. Copy the CURRENT properties.reported.collection.requestId (32 hexadecimal
   characters) into properties.desired.collection.requestId.
4. Set properties.desired.collection.enabled to true.
5. Set properties.desired.collection.expiresAtUtc to a future UTC timestamp,
   exactly YYYY-MM-DDTHH:mm:ssZ. Maximum validity is seven days. Default helper
   preparation uses 72 hours.
6. Save. Verify reported collection.state changes to collecting.
7. To stop, set desired collection.enabled=false and save.
8. After a power cycle, repeat with the new reported request ID. A transient
   network reconnect in the SAME boot may resume a still-valid approval after
   synchronizing the current twin. Expired approval stops forwarding.

The cloud's initial desired collection is:
```json
{"enabled": false, "requestId": "", "expiresAtUtc": ""}
```

Do not edit reported properties or service-managed metadata/version fields.
The optional Windows popup below makes pending requests visible on Ahmed's PC.
Approval still takes place in the device twin. No email notification is enabled.

### Windows approval popup

Run scripts/Watch-GatewayApproval.ps1 with Windows PowerShell, or use the
ITP Approval Notifications desktop shortcut. A tray icon remains active.
When the gateway reports awaiting_approval with a valid startup request, a
window appears with the observed request ID and an Open Azure Portal button.
In the Portal, open Devices -> azure-gateway-01 -> Device twin and approve using
the latest reported request ID. The popup never writes desired properties or
grants permission. Dismissing it leaves forwarding blocked.

- The PC must be awake, online, signed in with Azure CLI, and running the watcher.
  This is a Windows popup, not a native Azure Portal notification or phone alert.
- Checks run every three minutes, plus any manual Check now from the tray menu.
  Startup checks immediately. Azure's query index may add some delay.
- The query projects four small status fields on the server, rather than
  downloading the full twin and its metadata on each poll. At 180 seconds there
  are at most about 480 scheduled queries per full day, plus manual checks.
  Queries consume IoT Hub quota based on result size; continue to monitor the
  F1 daily limit. Do not run multiple watchers or reduce the interval.
- A pending boot alerts once while the watcher runs. Returning to a non-pending
  state rearms the alert, so a later expiry/revocation can also request attention.
  A new boot request alerts again. Restarting the watcher also checks pending
  requests. Closing the watcher stops all notifications.
- Right-click the tray icon for Check now, Open Azure Portal, a clearly marked
  preview, or Exit notifications. The watcher is single-instance per Windows
  session and has not been registered to start automatically at Windows login.
- Network/sign-in failures do not approve anything. The tray indicates failure
  and the watcher retries. Run az login yourself if the CLI session expires.
- A demo preview and read-only check are available without restarting hardware:

```powershell
.\scripts\Watch-GatewayApproval.ps1 -Preview
.\scripts\Watch-GatewayApproval.ps1 -CheckOnce
```

The preview is explicitly marked and does not query or modify Azure. End-to-end
automatic notification still needs a real gateway restart or approval-state
change; do not interrupt an official collection trial just to demonstrate it.

Metering reference: https://learn.microsoft.com/en-us/azure/iot-hub/iot-hub-devguide-pricing

### Helper commands (PowerShell, from repository root)

Fresh read-only cloud checks:
```powershell
.\scripts\Get-AzureReadiness.ps1
```

Optional -OutputFile writes a NEW local evidence snapshot and refuses to
overwrite an existing one. It does not modify Azure. Its checks deliberately
separate cloud configuration, reported firmware and physical delivery proof.

```powershell
.\scripts\Collection-Approval.ps1
.\scripts\Collection-Approval.ps1 -Action PrepareApproval -Hours 72 -ExperimentId normal-3nodes-trial01
```

Status and PrepareApproval do not change Azure. PrepareApproval prints the exact
collection object to paste into desired properties and can save a local trial
preparation record. It refuses to prepare a grant if the new gateway firmware
has not reported its request. A preparation record is NOT proof approval was
saved or accepted.

Explicit revocation, if required:
```powershell
.\scripts\Collection-Approval.ps1 -Action Revoke
```

The device twin exposes publishAcceptedBySource for quick checks. Those counters
are device-side publish results, not a substitute for counting stored records.

## Dataset labels in Azure

The 28 October overnight data is labelled setup_and_flagged_test. Container
research-raw retains the telemetry route and points, via metadata, to the private
research-metadata container. Its label_policy_json metadata field holds the
timestamp rules and source-specific quality flags. Closed files are labelled
through ordinary blob metadata and their raw SHA-256 hashes are verified.
Future/active files use the same record-time rules but do not automatically
receive blob metadata. No data is moved, deleted, repaired or called a clean
baseline. See the dataset guide for the exact boundary and reader command.

The task-specific scripts/Label-AzureSetupData.py supports a read-only plan by
default and an explicit --apply mode. It uses the existing Azure CLI Entra
session and the CLI's bundled Python/SDK. It requires a new --output-dir and
preserves earlier local label snapshots. --self-test checks classification
boundaries and source-specific flags. It does not change firmware, approval,
routes, role assignments or the teammate's adapters.

## Azure diagnostics

Deployed resources:
- Workspace law-crosscloud-research in rg-iot-project / Japan East.
- PerGB2018 usage-based SKU, 30-day retention.
- Daily ingestion cap 0.1 GB. It is a backstop, not an exact spending limit;
  ingestion can exceed the cap and logging stops when the cap is reached.
- Setting diag-crosscloud-research on iot-hub-crosscloud.
- Enabled Connections, DeviceTelemetry, Routes, DeviceIdentityOperations,
  D2CTwinOperations, C2DTwinOperations, DirectMethods and AllMetrics.
- Log delivery was verified by querying C2DTwinOperations records.
- Resource logs arrive with delay. They are operational events, not a copy of
  every sensor measurement. The full raw dataset is in research-raw.

Use the Azure Portal IoT Hub Logs view and the queries in
cloud/azure/diagnostics/queries.kql. Preserve configuration and trial evidence
for the report. The diagnostics JSON files record the selected categories.

The hub remains F1 and the existing route still uses key-based storage
authentication. Device authentication remains SAS, not X.509 client certificates.
An X.509 migration is still a separate cybersecurity milestone; this approval
feature does not constitute that migration.

## Dataset access for Hashem

Hashem's Microsoft identity and existing assignments were verified. At the
user's explicit request, existing Reader, Storage Account Contributor and
Storage Blob Data Owner assignments were left unchanged. His account therefore
has broader rights than a technical read-only role. The agreed APPLICATION
workflow is read-only; no restrictive RBAC change was made.

Use [the Azure dataset access handover](azure-dataset-access.md). He connects
directly to Blob Storage using his own Entra login and his existing adapters.
No account keys, shared passwords, mail attachments or duplicate ingestion
application are needed. His own sign-in/download test remains his responsibility.

## One-time hardware deployment and acceptance

1. Fit all four antennas securely and use stable USB power.
2. Check the shared home/hotspot uplink values, leaving private sensor values
   stable. Flash the Azure gateway sketch first, then Node 1, Node 2 and Node 3
   to their correct physical boards. Select XIAO_ESP32C3 and 115200 baud.
3. Confirm gateway private address 192.168.77.1 and successful uplink/NTP/TLS/MQTT.
4. Confirm each sensor reports its correct ID and a private-LAN IP.
5. Before granting approval, verify the twin says awaiting_approval and no
   new telemetry from this boot is forwarded. Wait for already-buffered older
   route batches to drain; deleting stored blobs is not a test prerequisite.
6. Prepare/paste the Portal approval, then verify collecting and first publish
   counts for all three sources.
7. After the storage batching delay, check freshly stored blobs contain all
   three source.device_id values and coherent source.sequence values.
8. Run a short 10-15 minute test before leaving a multi-day run. Record actual
   start/end, source IP mapping, power/network changes and approval gaps.
9. Revoke and verify forwarding stops. Power-cycle the gateway and verify the
   previous grant is rejected; approve the new request explicitly.
10. For a home-to-hotspot demonstration, change only the uplink fields and
    reflash the gateway. Sensors keep the same private network/address.
11. Start the official trial only after these gates pass. Keep observations,
    intervention labels and setup/test data separated in the teammate's workflow.

The new gate blocks forwarding; sensors may continue reading locally and
sending UDP while approval is pending. They are not given cloud credentials.

## Scope and remaining evidence

The checked raw snapshot establishes three-node Azure delivery for that window.
The later check establishes corrected Node 3 values in storage, but does not
establish sensor accuracy or uninterrupted collection. Keep those states
separate in the report and diagram. No email was
sent to Hashem, his roles were not
changed, no new dataset was fabricated, and no data-science adapters were rebuilt.

References:
- https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html
- https://docs.espressif.com/projects/esp-idf/en/release-v5.1/esp32c3/api-reference/network/esp_wifi.html
- https://learn.microsoft.com/en-us/azure/iot-hub/iot-mqtt-connect-to-iot-hub
- https://learn.microsoft.com/en-us/azure/iot-hub/iot-hub-devguide-pricing
- https://learn.microsoft.com/en-us/azure/iot-hub/monitor-iot-hub
- https://learn.microsoft.com/en-us/azure/azure-monitor/logs/daily-cap
