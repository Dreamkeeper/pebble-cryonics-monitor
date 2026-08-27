# Draft PR to coredevices/mobileapp — pending owner approval to open

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
