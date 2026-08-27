# Draft: upstream request for DataLogging support in PebbleKitAndroid2

Target: https://github.com/pebble-dev/PebbleKitAndroid2/issues (and/or
the Core Devices mobile-app tracker, since delivery requires the phone
app to forward sessions). Not yet posted — owner review first.

---

**Title: Feature request: DataLogging delivery to companion apps
(background-worker liveness for a safety-critical app)**

## Use case

We build an open-source unresponsiveness monitor for cryonicists
(https://github.com/Dreamkeeper/pebble-cryonics-monitor, GPL-3.0):
detectors for pulse-signal loss, falls, and non-motion run in a Pebble
**background worker** 24/7, with an Android companion that escalates to
human contacts. The worker architecture is essential — the wearer keeps
their own watchface — but workers cannot use AppMessage, so
**DataLogging is the only channel a worker has toward the phone**
(SDK docs list it as one of the three worker↔phone communication
options for exactly this reason).

Our worker logs a 14-byte heartbeat record every 60 s (session tag
0xC202, `DATA_LOGGING_BYTE_ARRAY`). Firmware-side this works fine —
sessions buffer and survive as documented. The phone side is the gap:

## What we measured (2026-08-27, Pebble Time 2, Core app + PK2 1.2.0)

- **PebbleKitAndroid2 1.2.0 exposes no DataLogging API** (verified
  against the published artifacts: `BasePebbleListenerService` offers
  message/app-opened/app-closed callbacks only).
- We implemented the **legacy PebbleKit broadcast protocol** as a
  fallback receiver (`com.getpebble.action.dl.RECEIVE_DATA` /
  `ACK_DATA` / `REQUEST_DATA`, byte-array payloads base64-encoded in
  `pbl_data_object`, per the original PebbleKit-Android source). During
  a 10-minute adb-monitored session — BT connected, worker logging
  every 60 s, `REQUEST_DATA` broadcast every 60 s — **zero broadcasts
  were delivered**. The current Core app apparently does not forward
  DataLogging to third-party companions via the legacy protocol either.

## Why it matters beyond us

Any worker-based app (sleep trackers, health loggers, safety monitors)
currently has no way to move data off the watch without the wearer
opening the watchapp. For a safety application this means the phone
cannot distinguish "worker alive, nothing to report" from "worker
evicted/dead" — a liveness gap we currently paper over by periodically
launching our watchapp (which takes the screen) on a user-configured
interval.

## Ask

1. A DataLogging receive API in PebbleKitAndroid2 (e.g. a
   `BasePebbleListenerService` callback per session tag, with ack
   semantics), with the Core app forwarding sessions to the companion
   that owns the app UUID — mirroring `onMessageReceived`.
2. Alternatively/meanwhile: have the Core app emit the legacy
   `com.getpebble.action.dl.*` broadcasts so existing PebbleKit-Android
   receivers work unchanged.

We are happy to test dev builds on real hardware (Pebble Time 2 +
Android 15/HyperOS) and can share our receiver implementation and
measurement setup. Since PebbleOS is open source we can also help
verify the firmware side, though our measurements suggest the firmware
is already doing its part — the gap is in mobile-side delivery.

---

## Note on the "test firmware" idea (owner suggestion)

Building a custom PebbleOS image is possible now that the OS is open
source, but S5's evidence points at the *mobile* stack, not firmware:
the worker's `data_logging_log()` calls succeed and sessions persist
(firmware behaving as documented); nothing arrives on Android because
the Core app doesn't forward it and PK2 has no API for it. A custom
firmware cannot fix a phone-side gap — the leverage is in the Core
app / libpebble3 / PK2, hence the upstream request. Where a custom
firmware COULD help later: exposing HR sensor quality/off-body flags
(our S4 finding: the stock firmware serves a frozen last-bpm value
off-body), which would upgrade wear detection from inference to fact.
