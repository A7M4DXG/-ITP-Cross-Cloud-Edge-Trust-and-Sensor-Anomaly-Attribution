#pragma once
#include <stddef.h>
#include <string.h>

// IDs identify claimed sources; UDP source IDs are not cryptographic identities.
struct SensorSequenceState {
  const char* id;
  unsigned long lastSequence;
  bool hasSequence;
};

static SensorSequenceState sensorStates[] = {
  {"sensor-node-01", 0, false},
  {"sensor-node-02", 0, false},
  {"sensor-node-03", 0, false}
};
static const size_t SENSOR_COUNT = sizeof(sensorStates) / sizeof(sensorStates[0]);

static int findSensorIndex(const char* sourceId) {
  if (sourceId == nullptr) return -1;
  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    if (strcmp(sensorStates[i].id, sourceId) == 0) return (int)i;
  }
  return -1;
}

static const char* classifySensorSequence(int index, unsigned long sequence) {
  if (index < 0 || (size_t)index >= SENSOR_COUNT) return "INVALID";
  const SensorSequenceState& state = sensorStates[index];
  if (!state.hasSequence) return "FIRST";
  if (sequence <= state.lastSequence) return "REPLAY_OR_DUPLICATE";
  // Subtract only after ordering to avoid overflow at ULONG_MAX.
  if (sequence - state.lastSequence > 1) return "SEQUENCE_GAP";
  return "NORMAL";
}

static void recordSensorSequence(int index, unsigned long sequence) {
  if (index < 0 || (size_t)index >= SENSOR_COUNT) return;
  SensorSequenceState& state = sensorStates[index];
  if (!state.hasSequence || sequence > state.lastSequence) {
    state.lastSequence = sequence;
    state.hasSequence = true;
  }
}
