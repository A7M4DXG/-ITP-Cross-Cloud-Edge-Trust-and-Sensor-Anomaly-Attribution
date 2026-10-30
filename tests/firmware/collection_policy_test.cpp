#include "../../firmware/azure-gateway/collection_policy.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main() {
  const char* boot = "11111111222222223333333344444444";
  const char* other = "aaaaaaaa222222223333333344444444";
  const uint64_t now = 1800000000;
  CollectionApproval approval;
  approval.reset(boot);
  assert(!approval.allowed(now));
  assert(!approval.apply(true, other, now + 3600, now));
  assert(!approval.apply(true, boot, now, now));
  assert(!approval.apply(true, boot, now + MAX_APPROVAL_SECONDS + 1, now));
  assert(!approval.apply(true, boot, now + 3600, 0));
  assert(approval.apply(true, boot, now + 3600, now));
  assert(approval.allowed(now + 3599));
  assert(!approval.allowed(now + 3600));
  assert(!approval.apply(false, boot, now + 3600, now));
  assert(!approval.allowed(now));
  assert(approval.apply(true, boot, now + 72 * 3600, now));
  approval.reset(other);
  assert(!approval.allowed(now));
  assert(!approval.apply(true, boot, now + 3600, now));

  assert(telemetryUnits(0) == 0);
  assert(telemetryUnits(355) == 1);
  assert(telemetryUnits(512) == 1);
  assert(telemetryUnits(513) == 2);
  assert(canReserveTelemetry(0, 1));
  assert(canReserveTelemetry(6999, 1));
  assert(!canReserveTelemetry(7000, 1));
  assert(!canReserveTelemetry(6999, 2));
  assert(!canReserveTelemetry(UINT32_MAX, 1));
  assert(!canReserveTelemetry(0, 0));

  ForwardCadence cadence;
  assert(cadence.ready(0, 1000));
  cadence.record(0, 1000);
  assert(!cadence.ready(0, 38999));
  assert(cadence.ready(0, 39000));
  assert(cadence.ready(1, 1000));
  assert(cadence.ready(2, 1000));
  assert(!cadence.ready(3, 1000));
  cadence.record(2, UINT32_MAX - 1000);
  assert(!cadence.ready(2, 1000));
  assert(cadence.ready(2, 40000));
  puts("PASS: approval default deny, stale boot, revocation, expiry, quota boundaries and three-source cadence");
}
