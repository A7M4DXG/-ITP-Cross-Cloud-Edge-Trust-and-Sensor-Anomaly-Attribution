#include "../../firmware/azure-gateway/sensor_registry.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static void expectStatus(int index, unsigned long sequence, const char* status) {
  assert(strcmp(classifySensorSequence(index, sequence), status) == 0);
}

int main() {
  const int one = findSensorIndex("sensor-node-01");
  const int two = findSensorIndex("sensor-node-02");
  assert(one >= 0 && two >= 0 && one != two);
  const int three = findSensorIndex("sensor-node-03");
  assert(three >= 0 && three != one && three != two);
  assert(findSensorIndex("sensor-node-04") == -1);
  assert(findSensorIndex(nullptr) == -1);

  expectStatus(one, 1, "FIRST");
  recordSensorSequence(one, 1);
  expectStatus(two, 1, "FIRST"); // Equal sequence numbers from different nodes are valid.
  recordSensorSequence(two, 1);
  expectStatus(three, 1, "FIRST");
  recordSensorSequence(three, 1);
  expectStatus(three, 2, "NORMAL");
  expectStatus(one, 2, "NORMAL");
  expectStatus(two, 2, "NORMAL");

  expectStatus(one, 100, "SEQUENCE_GAP");
  recordSensorSequence(one, 100);
  expectStatus(two, 2, "NORMAL"); // Busy Node 1 does not advance Node 2's counter.
  recordSensorSequence(two, 2);
  expectStatus(one, 100, "REPLAY_OR_DUPLICATE");
  recordSensorSequence(one, 50);
  expectStatus(one, 101, "NORMAL"); // Replays cannot lower the high-water mark.

  recordSensorSequence(-1, ULONG_MAX);
  recordSensorSequence(99, ULONG_MAX);
  expectStatus(-1, 3, "INVALID");
  expectStatus(two, 3, "NORMAL"); // Unknown sources cannot poison known state.
  expectStatus(two, 1, "REPLAY_OR_DUPLICATE"); // A reboot is not silently trusted.

  recordSensorSequence(one, ULONG_MAX - 1);
  expectStatus(one, ULONG_MAX, "NORMAL");
  recordSensorSequence(one, ULONG_MAX);
  expectStatus(one, ULONG_MAX, "REPLAY_OR_DUPLICATE");
  expectStatus(one, 0, "REPLAY_OR_DUPLICATE");

  puts("PASS: three-node isolation, interleaving, replay, unknown-source and boundary checks");
}
