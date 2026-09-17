# Cross-Cloud Edge Trust and Sensor Anomaly Attribution

## Overview

This project investigates trust, security, and anomaly attribution
in IoT sensor data across Azure and AWS cloud environments.

The system uses ESP32-C3 sensor nodes and dedicated edge gateways
to collect, authenticate, transmit, and evaluate sensor telemetry.

## Architecture

Sensor Nodes
    ↓
Edge Gateways
    ↓
MQTT / TLS
    ↓
Azure IoT Hub / AWS IoT Core
    ↓
Telemetry + Provenance
    ↓
Anomaly Detection & Attribution

## Hardware

- Seeed Studio XIAO ESP32-C3 × 4
- DHT11 sensors
- BH1750 sensor
- Breadboards
- Jumper wires
- USB-C cables

## Device Roles

| Device | Role |
|---|---|
| XIAO #1 | Sensor Node 1 |
| XIAO #2 | Sensor Node 2 |
| XIAO #3 | Azure Gateway |
| XIAO #4 | AWS Gateway |

## Cloud Platforms

- Microsoft Azure IoT Hub
- AWS IoT Core

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