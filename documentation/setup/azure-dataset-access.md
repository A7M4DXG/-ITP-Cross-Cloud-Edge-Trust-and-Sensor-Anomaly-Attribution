# Hashem: receiving the complete raw Azure dataset

The Azure deployment uses one gateway and three sensor nodes. Ahmed handles
hardware, Azure administration, diagnostics and manual collection approval.
Keep your existing adapters and models; no replacement pipeline is required.

Nodes 1-2 use blue DHT11 sensors. Node 3's white sensor is expected to be a
DHT22/AM2302; confirm the marking and record its model separately. Its DHT11
decoder produced about 2.1-2.2 C during setup. Subsequent serial readings near
29.8 C begin at 2026-10-27T16:22:35Z, and a corrected stored record is verified at
2026-10-27T16:23:15Z. Earlier raw values must remain unchanged and labelled as setup
data. Node 2's constant 88.6% humidity needs a co-location/reference check.
Do not treat this mixed-model run as a three-identical-sensor comparison.

## Connection details

| Setting | Value |
| --- | --- |
| Account URL | https://crosscloudresearch01.blob.core.windows.net |
| Container | research-raw |
| IoT Hub / authenticated cloud device | iot-hub-crosscloud / azure-gateway-01 |
| Sensor payload IDs | sensor-node-01, sensor-node-02, sensor-node-03 |
| Sampling | 40 seconds per node; about 6,480 readings per full day TOTAL |
| Schema currently emitted | 1.0; preserve all existing nested fields |
| Azure region | Japan East |
| University tenant | 7e0b5fcf-12c4-4eff-96b6-4664f25dc7da |
| Subscription | 511ecd6d-4d4c-4322-9eba-e8d224e9b7dd |

Your existing permissions were verified and left unchanged by Ahmed's request.
Use YOUR own university account. This handover asks for read-only data access
even though your existing roles also permit administration and deletion.

## Access through Azure CLI

After installing Azure CLI, open a new terminal:
```powershell
az login --tenant 7e0b5fcf-12c4-4eff-96b6-4664f25dc7da
az account set --subscription 511ecd6d-4d4c-4322-9eba-e8d224e9b7dd
az storage blob list --account-name crosscloudresearch01 --container-name research-raw --auth-mode login --output table
```

For a manual full raw export, choose a NEW local destination directory:
```powershell
New-Item -ItemType Directory -Path .\azure-raw-trial01
az storage blob download-batch --account-name crosscloudresearch01 --source research-raw --destination .\azure-raw-trial01 --auth-mode login
```

The download preserves the blob directory structure. Do not delete or modify the
cloud data. For incremental operation use your existing Azure Blob reader/SDK
with AzureCliCredential or your configured DefaultAzureCredential.
No account key or SAS token needs to be sent to you.

The durable full dataset is in Blob Storage, not the temporary IoT Hub event
buffer. Do not use a live IoT Hub monitor as your historical dataset source.

## Azure labels for the 28 October overnight test

The raw container's Metadata panel identifies it as setup_and_flagged_test and
points to the private research-metadata container. That container intentionally
stores the portable policy in its metadata field label_policy_json, rather than
as a telemetry blob. No new role assignments were made. Retrieve it with:

```powershell
az storage container metadata show --account-name crosscloudresearch01 --name research-metadata --auth-mode login --query label_policy_json --output tsv
```

Apply the policy using gateway.received_at after decoding the original records:

- Before 2026-10-27T16:22:35Z: setup.
- At/after that timestamp: flagged_test, until a later policy supersedes it.
- Node 3 before that boundary: pre_sensor_type_correction.
- Node 2: humidity_reference_check_pending; 88.6% is an observation to check,
  not independently established anomaly ground truth.

The boundary is 28 October 00:22:35 Malaysia time. It identifies the first
plausible post-correction Node 3 reading in the supplied log, not a declared
formal baseline start. No clean baseline is established by these labels.

Existing closed raw files also have itp_phase, itp_quality and itp_label_set in
their blob metadata. mixed_setup_test means the file crosses the boundary;
classify its individual records by timestamp. Recent active files and future
files do not inherit blob metadata automatically, so the portable policy is
the authority for those records too. This was a one-time labelling operation,
not a background labelling service.

Metadata writes update Azure ETag/Last-Modified properties; the operation saved
their prior values and verified unchanged raw payloads with SHA-256. Use the
payload gateway timestamp and Azure enqueue time for experiment chronology,
not the blob's metadata-update time. Keep labels alongside your existing
pipeline outputs without rewriting source measurements or validation flags.

## Data handling reminders for the updated deployment

- Discover every partition/date directory under the selected experiment window;
  do not hardcode a single blob path. Routing batches may take a few minutes.
- The current gateway retains the same schema-1.0 payload and Base64 Body
  behavior, so existing object/text/Base64-compatible adapters remain useful.
- Keep Azure enqueue and gateway receipt timestamps separate. There is no
  invented sensor UTC clock, in-band experiment ID or receipt ID in schema 1.0.
- Group counters and reliability statistics by sensor, gateway and an explicit
  trial/session window. The new sensors persist sequence counters across normal
  power cycles; do not infer a session boundary solely from sequence=1.
- Gateway power cycles require new human approval. Exclude approval-pending
  intervals from an active collection window, or label them separately; do not
  classify them as unexplained sensor loss.
- Use the ACTUAL source IP mapping for the private sensor LAN. DHCP lease order
  can vary; never assume Node 1 is .2, Node 2 is .3, etc.
- Use interval_seconds=40 wherever your adapter/model/observation-window setup
  requires cadence. Do not reuse a five-second or 60-second model assumption.
- For a full 24-hour active run the target is around 2,160 readings PER NODE,
  6,480 TOTAL. Account for partial days, DHT failures, network outages, approval
  pauses and batching tail before computing completeness.
- Equal DHT11 readings and repeated sequences are evidence to preserve/flag, not
  records to silently delete as ingestion duplicates.
- Saved synthetic study metrics are not measurements of these physical nodes.
- Preliminary old test data was archived and the container cleared on 27 October.
  New research data is not to be deleted by the ingestion process.
- Confirm actual newly stored records for all three nodes before starting the
  multi-day comparison. Firmware compilation alone does not establish delivery.

## Handover evidence to request from Ahmed

Trial ID, actual UTC collection window, source IP map, firmware revision, actual
cadence, approval/reboot/network interruption log and the relevant Azure
configuration snapshot. Keep your experiment labels and train/validation/test
splits separate from the immutable raw telemetry.

Before analysis, verify you can list/download a real blob under your own login,
decode it, and identify each of the three source IDs. No new read operation by
Ahmed proves your own login works; perform that check on your machine.

Reference:
https://learn.microsoft.com/en-us/azure/storage/blobs/authorize-data-operations-cli
