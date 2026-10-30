# ITP - Edge Trust and Sensor Anomaly Attribution

## Overview

This project investigates trust, security, and anomaly attribution
in IoT sensor data using Microsoft Azure and a separate data-science workstream.

The system uses ESP32-C3 sensor nodes and one Azure edge gateway
to collect, authenticate, transmit, and evaluate sensor telemetry.

## Current Azure collection deployment

The active workstream uses **one Azure gateway and three sensor nodes**.
Nodes 1-2 use blue DHT11 sensors. Node 3 has a white sensor, expected to be a
DHT22/AM2302; its corrected DHT22 readings have now reached Azure Storage.
Confirm the physical marking when recording the hardware inventory.
The gateway provides a private sensor Wi-Fi network at
192.168.77.1 and uses home Wi-Fi or a phone hotspot for its internet uplink.
Sampling is 40 seconds per sensor: about 6,480 readings/day total, with a
7,000-telemetry-unit daily ceiling and manual per-boot collection approval.

Cloud diagnostics and denied-by-default twin properties are configured.
On 28 October Malaysia time, all three source IDs were verified in stored raw
blobs, including corrected Node 3 readings near 29.8 C. Node 2's constant high
humidity still needs a co-location/reference check. This is a verified short
collection window, not an established uninterrupted multi-day baseline.
AWS is outside the current project scope; any old AWS folders are inactive scaffolding.

- [Operator guide and upload/approval steps](documentation/setup/azure-operations.md)
- [Windows popup watcher for pending approvals](scripts/Watch-GatewayApproval.ps1)
- [Dataset access for the data-science teammate](documentation/setup/azure-dataset-access.md)
- [Azure architecture diagram](diagrams/azure-three-sensor-flow.mmd)

## Architecture

Sensor Nodes
    ↓
Azure Edge Gateway
    ↓
MQTT / TLS
    ↓
Azure IoT Hub -> Azure Blob Storage
    ↓
Telemetry + Provenance
    ↓
Anomaly Detection & Attribution

## Hardware

- Seeed Studio XIAO ESP32-C3 × 4
- DHT11 sensors
- White sensor on Node 3: DHT22/AM2302 expected; verify the marking
- External antennas for all four boards
- Breadboards
- Jumper wires
- USB-C cables

## Device Roles

| Device | Role |
|---|---|
| sensor-node-01 | Sensor Node 1 + DHT11 |
| sensor-node-02 | Sensor Node 2 + DHT11 |
| sensor-node-03 | Sensor Node 3, configured DHT22; corrected readings verified in storage |
| azure-gateway-01 | Shared Azure gateway, private sensor AP and internet uplink |

## Cloud Platforms

- Microsoft Azure IoT Hub
- Azure Blob Storage and Azure Monitor

## Communication

- Wi-Fi
- MQTT
- TLS
- Device authentication
- Access control
- Provenance metadata

## Research Focus

The project investigates whether sensor data alone or sensor data
combined with gateway context and provenance provides stronger
anomaly detection and attribution.

## Experimental Scenarios

- Normal sensor data
- Physical spike/drift
- Replay/duplicate data
- Malformed data
- Unexpected source
- Fabricated values through an authorized path

## Final firmware handoff

[Final code and Arduino upload checklist](documentation/setup/final-code-handoff.md).
