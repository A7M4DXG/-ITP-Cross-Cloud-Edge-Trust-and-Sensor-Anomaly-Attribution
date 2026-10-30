#pragma once
#include <stdint.h>
#include <stddef.h>

static constexpr uint32_t SENSOR_SAMPLING_INTERVAL_MS = 40000;
static constexpr size_t SENSOR_NODE_COUNT = 3;
// Permit small scheduling jitter without dropping every second 40-second sample.
static constexpr uint32_t MIN_FORWARD_INTERVAL_MS = 38000;
static constexpr uint32_t MAX_TELEMETRY_UNITS_PER_UTC_DAY = 7000;
static constexpr size_t MAX_TELEMETRY_BODY_BYTES = 512;
static constexpr uint32_t STATUS_REPORT_INTERVAL_MS = 900000;
static constexpr uint32_t MAX_APPROVAL_SECONDS = 7 * 24 * 60 * 60;
static constexpr uint32_t MIN_VALID_UTC = 1700000000;
static constexpr char GATEWAY_MDNS_NAME[] = "itp-azure";
static constexpr char SENSOR_GATEWAY_IPV4[] = "192.168.77.1";
static constexpr char EXPECTED_GATEWAY_ID[] = "azure-gateway-01";
static constexpr uint16_t GATEWAY_UDP_PORT = 5000;
