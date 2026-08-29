# Alarm delivery hardening: episodes, ACKs, reconciliation, monotonic time

## Why

The adversarial codebase review (2026-08-29,
[CODEBASE-REVIEW-FINDINGS.md](../../../docs/CODEBASE-REVIEW-FINDINGS.md))
confirmed four related defects in the alarm path — the one path that
must never fail silently:

1. **The confirmed ALARM is fire-and-forget** (finding 1). One
   AppMessage, no delivery result checked, no retry, and the
   DataLogging backup channel discards the stage. A busy outbox or a
   brief Bluetooth drop at the worst moment loses the alarm entirely:
   no siren, no escalation, no responders.
2. **A delayed foreground launch can drop the only parked safety
   action** (finding 3) — the 60 s staleness guard added against nag
   replay also applies to CHECKIN/COUNTDOWN/ALARM, and nothing
   reconciles an active ladder stage on startup.
3. **Watch-side cancel lies on failure** (finding 9): the UI clears
   immediately while the `WMSG_USER_OK` to the worker is unchecked —
   the ladder can march to ALARM after the wearer saw a
   successful-looking cancellation.
4. **Detector time is wall-clock** (finding 11): a phone-synced time
   correction reads as a huge (or negative→2³²) elapsed interval and
   can latch ALARM before a countdown's promised fuse, or blow past
   detector windows.

All four share one missing primitive: alarms have no durable identity.
The fix is an **episode ID** minted by the detector core when a ladder
episode starts, carried end-to-end (worker → app → phone → server),
acknowledged at each hop that matters, and usable for idempotent
recovery from any channel.

## What changes

- **Episodes (core):** the detector core assigns a persistent-sequence
  16-bit episode ID when a ladder episode begins; every emitted action
  and heartbeat carries it. One episode = one alarm identity from
  first CHECKIN to resolution.
- **Delivery ACK + retry (watch app ↔ phone):** PRE_ALARM/ALARM/CANCEL
  messages carry the episode ID; the companion answers with a new
  `PMSG_ALARM_ACK`; the watch app registers outbox callbacks and
  retries with backoff until the app-level ACK arrives or the episode
  ends. The phone deduplicates by episode ID.
- **DataLogging as authoritative recovery:** heartbeat record v3 adds
  the episode ID and detector; a record reporting COUNTDOWN/ALARM for
  an un-escalated episode triggers the full phone alarm path
  (deduplicated by episode). The spooled channel becomes a true
  backup for the live one.
- **Startup reconciliation (app):** parked ladder actions no longer
  expire by age (only informational nags do); on every foreground
  start the app requests worker status — which now includes stage,
  detector, episode, and stage deadline — and reconstructs any active
  alert UI + phone notification it finds.
- **Cancel ACK (app ↔ worker):** SELECT during a ladder stage shows
  "Cancelling…" and the UI clears only on the worker's existing
  `ALERT_CANCELLED` echo; unacknowledged cancels retry and then fail
  loudly (phone FAULT) instead of silently dismissing.
- **Monotonic detector time (worker):** the worker feeds the core a
  monotonic millisecond clock derived from its tick stream; wall
  clock remains only for display, DL record epochs, and persisted
  suspension deadlines (converted at the boundary, with jump
  detection). Core code is unchanged by construction — it already
  just consumes `now_ms`.
- **Firmware-gated quality metric (finding 7, small):** the companion
  enables the raw-quality gate only when the connected watch reports
  a dev firmware version tag (the diag build), so a persisted flag
  can never crash a stock-firmware worker after a downgrade.

## Impact

- Affected specs: `watch-phone-protocol` (delivery guarantees, v3
  record, new messages), `detector-ladder` (episode identity,
  monotonic time)
- Affected code: `watchapp/src/core/detectors.{h,c}` (+154-check host
  suite grows), `watchapp/src/core/protocol.h`, worker + app shells,
  companion (`MonitorService`, `DataLogReceiver`, `WatchLink`,
  `SettingsStore`), server untouched (already idempotent per the
  review fixes)
- Protocol: PMSG_ALARM_ACK=18, message key EPISODE=11, heartbeat
  record v3 (16 bytes), WMSG_STATUS repack
- Compatibility: v2 (14-byte) records remain parseable; a new
  companion with an old watchapp degrades to today's behavior
  (no episode → no ACK expected); an old companion with a new
  watchapp ignores the extra key and the watch retries expire after
  the bounded backoff — no regression.
- Out of scope: broadcast sender authentication (finding 12-crypto —
  tracked for the PK2 migration), server-side changes.
