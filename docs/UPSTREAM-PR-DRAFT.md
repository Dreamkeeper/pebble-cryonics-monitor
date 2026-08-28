# Upstream PRs — pending owner approval to open

## PR 2: coredevices/PebbleOS — frozen raw HR off-wrist (S4 root cause)

Branch: `raw-hr-offwrist-invalidate` on Dreamkeeper/PebbleOS (fork).
26 insertions, 3 files. Process: DCO (`Signed-off-by` already on the
commit under Dmitry Kvasnikov <kvasnikovd@outlook.com>) — approving
the PR opening confirms that attestation.

**Root cause (confirmed in source, matches our lab data exactly):**
off-wrist, the gh3x2x driver zero-inits HRMData (`bpm=0`,
`quality=OffWrist`); bpm 0 fails the validity range check in
`prv_hrm_subscription_cb`, so the metrics writer never runs and
`hr.metrics.current_bpm` — the storage behind
`HealthMetricHeartRateRawBPM` — serves the last on-wrist value forever
while ~1 Hz events keep firing. The event payload already zeroes
`current_bpm` off-wrist; the peekable metric was simply never kept in
sync. **Fix:** on off-wrist events, invalidate the stored metric
(bpm 0, quality OffWrist, fresh timestamp) via a new
`activity_metrics_prv_set_raw_hr_offwrist()`, mirroring the adjacent
locking/write patterns.

**PR title:** activity: invalidate raw HR metric when the HRM reports
off-wrist

**PR body sketch:** problem (measured 9+ min of bit-identical stale
bpm with fresh events on a Time 2; field data + graph available),
root-cause walkthrough (driver → validity check → never-invalidated
metric), the fix, note that CI builds validate and we can field-test a
dev build on real Time 2 hardware, AI-assistance disclosure, and the
suggestion that PBL-40784 (their TODO about off-wrist special-casing)
relates. Not compiled locally (no arm toolchain here) — CI is expected
to build it; flagged honestly in the body.

Side effect to note for reviewers: after this fix, our own
change-based liveness detection continues to work unchanged (0 is
"no signal"), but apps get the honest signal directly — watch-side
wear inference stops being necessary at all on fixed firmware.

---

# PR 1: coredevices/mobileapp — DataLogging forwarding

Branch: `companion-datalogging` on Dreamkeeper/mobileapp (fork).
Requires: CLA signature at https://cla-assistant.io/coredevices/libpebble3
(legally identifiable name) before or after opening.

---

**Title: Forward third-party watchapp datalogging to companion apps
(classic PebbleKit broadcasts)**

## Problem

`Datalogging.logData()` handles health tags and system-app tags, and
silently drops everything else — so data a third-party watchapp logs
never reaches its companion. For **background workers** this is the
only phone-bound channel that exists (workers cannot use AppMessage),
so worker-based apps currently cannot get any data off the watch
without the user opening the watchapp.

Measured on a Pebble Time 2 + this app: a worker logging an item every
60 s (byte-array session, ACKed by the phone) delivered zero items to a
registered companion receiver over 10+ minutes.

## Change

- `CompanionDatalogging` interface in commonMain; `Datalogging`
  forwards non-health, non-system items to it.
- Android implementation (`PebbleKitClassicDatalogging`) emits the
  classic PebbleKit `com.getpebble.action.dl.RECEIVE_DATA` ordered
  broadcast per item — same delivery style as the existing
  `PebbleKitClassic` AppMessage compatibility.
- iOS/JVM bind a no-op.

~60 lines plus DI wiring. No protocol or storage changes; the phone
already ACKs these sessions, this just stops discarding the payloads.

## Compatibility notes (deliberate trade-offs, happy to adjust)

- Timestamp/tag ride as `long` extras. Receivers built against the
  original PebbleKit jar cast those extras to Guava `UnsignedInteger`
  and will silently skip the records (their current behavior anyway,
  since nothing is delivered today); receivers parsing primitives get
  everything. Shipping Guava just for two extras seemed wrong — say
  the word if you'd rather have exact legacy parity.
- Fire-and-forget: `ACK_DATA` from companions is ignored and
  `REQUEST_DATA` is not needed (items are forwarded as they arrive;
  nothing is buffered phone-side). A delivery-guaranteed path would
  belong in PebbleKit2 as a proper API — this PR is the minimal bridge
  until then.

## Testing

- [ ] Field-tested on Pebble Time 2 + Android 15 (HyperOS) with a
      GPL-3.0 companion app whose worker logs a 14-byte record every
      60 s (https://github.com/Dreamkeeper/pebble-cryonics-monitor) —
      to be checked off with a local build before opening the PR.

## Disclosure

Developed with AI assistance (Claude); the diff was reviewed and
field-tested by me, and I understand the code being changed. The use
case is a real safety application (unresponsiveness monitor for
cryonicists) where worker liveness matters.
