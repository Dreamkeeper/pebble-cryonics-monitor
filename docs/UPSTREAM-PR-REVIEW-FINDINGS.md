# Independent review findings for the upstream Pebble PRs

Date: 2026-08-28

This document reviews two proposed upstream commits:

- PebbleOS firmware: `00d9eae` on branch `raw-hr-offwrist-invalidate`
- Core mobile app: `78206b14` on branch `companion-datalogging`

Recommended disposition: request changes on both. The firmware patch needs a
small correctness fix and a regression test. The mobile patch needs a
session-aware DataLogging design before it is ready for upstream review.

## 1. PebbleOS firmware patch

### 1.1 Blocker: a nonzero off-wrist event undoes the invalidation

Locations:

- `src/fw/services/activity/activity.c:191-201`
- Existing reproducing input: `tests/fw/services/activity/test_activity.c:2559`

`valid_hr_reading` checks only the BPM range. For an event with `bpm=120` and
`quality=HRMQuality_OffWrist`, the new helper first stores zero, but
`activity_metrics_prv_add_median_hr_sample()` then immediately overwrites the
raw metric with 120. The existing test suite deliberately generates exactly
this input, although it currently verifies only the emitted event and not the
stored raw metric.

Suggested fix:

```c
const bool is_offwrist = hrm_event->bpm.quality == HRMQuality_OffWrist;
const bool valid_hr_reading =
    !is_offwrist &&
    hrm_event->bpm.bpm >= ACTIVITY_DEFAULT_MIN_HR &&
    hrm_event->bpm.bpm <= ACTIVITY_DEFAULT_MAX_HR;

if (is_offwrist) {
  activity_metrics_prv_set_raw_hr_offwrist(now_utc);
} else if (valid_hr_reading) {
  activity_metrics_prv_add_median_hr_sample(hrm_event, now_utc, now_uptime_ts);
}
```

### 1.2 Concern: no regression test verifies the stored raw metric

Add assertions after both `bpm=120/OffWrist` and `bpm=0/OffWrist` confirming
that:

- `ActivityMetricHeartRateRawBPM` is zero;
- `ActivityMetricHeartRateRawQuality` is `HRMQuality_OffWrist`;
- `ActivityMetricHeartRateRawUpdatedTimeUTC` advances;
- the next valid on-wrist event restores the BPM and quality.

### 1.3 Categories with no additional findings

- The recursive-mutex usage follows existing conventions and introduces no
  apparent deadlock or race.
- Setting the raw BPM to zero is consistent with the existing off-wrist event
  payload and existing zero-as-no-reading behavior.
- The system Health app checks raw quality before copying the BPM, so it keeps
  its last displayable heart rate instead of displaying zero.
- Helper naming and placement are consistent with the surrounding code.

## 2. Core mobile app DataLogging patch

### 2.1 Blocker: every item is falsely advertised as a byte array

Locations:

- `libpebble3/src/androidMain/kotlin/io/rebble/libpebblecommon/pebblekit/classic/io/rebble/libpebblecommon/pebblekit/classic/PebbleKitClassicDatalogging.kt:23-39`
- `libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/packets/DataLogging.kt:20-27`
- `libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/services/DataLoggingService.kt:48-55`

The adapter always sets `PBL_DATA_TYPE` to `TYPE_BYTES`. The watch protocol
supports `ByteArray`, `UInt`, and `Int`, and `OpenSession` already parses the
type. `DataLoggingSession` discards it, so integer logs are delivered through
the wrong PebbleKit callback with the wrong representation.

Suggested fix: retain `dataItemType` in `DataLoggingSession`, carry it through
`Datalogging` and `CompanionDatalogging`, and encode each classic PebbleKit
type according to its documented contract.

### 2.2 Blocker: fabricated session metadata violates the API contract

Location:

- `PebbleKitClassicDatalogging.kt:26-28`

Problems:

- `UUID.nameUUIDFromBytes("$appUuid/$tag")` assigns the same UUID to every
  successive or concurrent session using the same app and tag.
- `System.currentTimeMillis()` records delivery time rather than session
  creation time.
- The real watch timestamp is parsed in `DataLoggingIncomingPacket.OpenSession`
  but discarded.
- No `com.getpebble.action.dl.FINISH_SESSION` broadcast is emitted.

PebbleKit defines the log UUID as uniquely identifying one data log and the
timestamp as the time that log was first created.

Suggested interface shape:

```kotlin
fun onSessionOpened(
    watchId: WatchId,
    sessionId: UByte,
    appUuid: Uuid,
    timestamp: UInt,
    tag: UInt,
    itemType: DataItemType,
    itemSize: UShort,
)

fun onDataItems(watchId: WatchId, sessionId: UByte, data: ByteArray)
fun onSessionClosed(watchId: WatchId, sessionId: UByte)
```

Generate one UUID when a session opens, retain the watch-provided timestamp,
and emit `FINISH_SESSION` when it closes. Include watch identity because
session IDs are connection-local and can collide across watches.

### 2.3 Concern: the watch is ACKed before durable companion delivery

Location:

- `DataLoggingService.kt:66-80`

The watch receives an ACK before the broadcast is built. If no companion
receiver is active, Android drops the broadcast and the record is permanently
lost. The implementation ignores companion `ACK_DATA` and has no phone-side
queue.

Suggested fix: durably enqueue the record before ACKing the watch. If durable
delivery is intentionally outside the first PR, describe the bridge as
explicitly **best-effort** rather than as full classic DataLogging
compatibility.

### 2.4 Concern: unrestricted implicit broadcasts leak data and are unreliable

Location:

- `PebbleKitClassicDatalogging.kt:32-42`

Any installed application can register for the action and read every
third-party watchapp's logged records. In addition, applications targeting
Android 8 or newer generally cannot rely on manifest-declared receivers for
unrestricted implicit broadcasts.

Suggested fix: resolve the Android companion package from PBW/locker metadata
and call `intent.setPackage(packageName)`. A clearly labelled, opt-in legacy
fallback can remain for watchapps without companion-package metadata.

### 2.5 Concern: `dataId++` is unsafe across watch connections

Locations:

- `PebbleKitClassicDatalogging.kt:21`
- `PebbleKitClassicDatalogging.kt:37`
- `libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.kt:442,530`

`CompanionDatalogging` and `Datalogging` are singletons, while
`DataLoggingService` is connection-scoped. Multiple watch protocol coroutines
can therefore increment `dataId` concurrently, producing duplicate or lost
IDs. A Core-app process restart can also repeat ID 1 while the companion app's
process remains alive.

Suggested fix: use an `AtomicInteger`, keep values nonnegative when wrapping,
and seed or persist the sequence to reduce collisions across process restarts.
A bounded delivery queue would also move copying and Base64 work off the watch
protocol coroutine.

### 2.6 Concern: malformed or partial records are silently discarded

Location:

- `PebbleKitClassicDatalogging.kt:24-30,43`

Zero-sized items, payloads shorter than one item, and trailing partial items
are silently discarded. Since the watch was already ACKed, they cannot be
retried.

Suggested fix: validate `itemSize > 0` and
`data.size % itemSize.toInt() == 0` before ACK. Return a delivery result to
`DataLoggingService`; at minimum, log a warning containing the watch and
session metadata.

### 2.7 Concern: the new behavior has no automated tests

Add tests for:

- byte-array, unsigned-integer, and signed-integer sessions;
- multiple items and partial trailing data;
- one unique UUID and stable timestamp per session;
- close/finish delivery;
- concurrent delivery from two watches;
- exact Android intent-extra types;
- package-restricted broadcasts;
- behavior when no companion receiver is available.

### 2.8 Nit: the Guava incompatibility comment is outdated

Locations:

- `PebbleKitClassicDatalogging.kt:14-19`
- `docs/REVIEW-PROMPTS.md:147-151`

PebbleKit 2.6 changed the DataLogging API from Guava `UnsignedInteger` to
`java.lang.Long` and removed Guava. PebbleKit 4.x casts the timestamp and tag
extras to `Long`. The implementation's `Long` extras are correct; the warning
that standard current receivers will reject them is not.

Suggested fix: remove that warning from the source comment and PR description.

### 2.9 Nit: the JVM no-op-binding claim does not match the diff

Location:

- `libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.jvm.kt:3-4`

The JVM hunk adds two unused imports but no binding. Remove the imports and
correct the PR description, or add the intended binding when the JVM platform
module is implemented. The iOS no-op binding is present and structurally
correct.

### 2.10 Categories with no additional findings

- One Base64-encoded `UShort`-sized item remains comfortably below Android's
  Binder transaction limit.
- `sendOrderedBroadcast()` can be invoked from the protocol coroutine, though
  high-rate or large batches should eventually use a bounded queue.
- Android and iOS Koin constructor wiring is structurally sound; iOS remains
  deliberately unsupported through its no-op implementation.

## Primary references

- Pebble DataLogging guide:
  <https://developer.rebble.io/guides/communication/datalogging/>
- PebbleKit Android `PebbleDataLogReceiver` API:
  <https://developer.rebble.io/docs/pebblekit-android/com/getpebble/android/kit/PebbleKit.PebbleDataLogReceiver/>
- Official PebbleKit Android source and changelog:
  <https://github.com/pebble/pebble-android-sdk>
- Android broadcast restrictions and security guidance:
  <https://developer.android.com/develop/background-work/background-tasks/broadcasts>

## Recommended implementation order

1. Fix the firmware off-wrist branch and add the raw-metric regression test.
2. Redesign `CompanionDatalogging` around open/data/close session lifecycle and
   preserve timestamp and item type.
3. Add package-targeted Android delivery and decide whether the first version
   is durable or explicitly best-effort.
4. Make data IDs thread-safe, validate payload boundaries, and add tests.
5. Update the PR descriptions to remove the incorrect Guava statement and
   accurately state iOS/JVM scope.
