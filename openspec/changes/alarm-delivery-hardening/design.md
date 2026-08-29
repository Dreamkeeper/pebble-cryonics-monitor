# Design — alarm delivery hardening

## D1. Episode identity

- `cm_core` gains `uint16_t episode_seq` (current) minted when the
  ladder leaves `CM_STAGE_NONE` (CHECKIN or COUNTDOWN start; SOS
  included). The seed is loaded/persisted by the shell
  (`PK_EPISODE_SEQ`, +1 per episode, wraps at 65535) so IDs stay
  unique across worker restarts — a reboot mid-episode yields a new
  episode, which is correct: the old one is void.
- `cm_action` gains `uint16_t episode`. All ladder-related actions
  (CHECKIN_START, COUNTDOWN_START, ALARM, ALERT_CANCELLED) carry it;
  informational actions carry 0. Parked-action persistence
  automatically includes it (struct write).
- Episode 0 is reserved as "none/legacy" everywhere.

## D2. Watch→phone delivery ACK + retry

- New message key `KEY_EPISODE = 11` added to PRE_ALARM, ALARM,
  CANCEL, and NOTWORN/SENSOR_FAULT (informational ones carry it too
  when nonzero — free).
- New `PMSG_ALARM_ACK = 18` (phone→watch): SECONDS = episode ID.
  The companion sends it on receipt of PRE_ALARM/ALARM (not for
  informational messages).
- App-side sender: `send_alarm_to_phone(episode, ...)` registers
  `app_message_register_outbox_sent/failed`; a pending-alarm slot
  holds (msg, episode, attempt). Retry on outbox failure OR missing
  app-level ACK: backoff 1 s, 2 s, 4 s, 8 s, then every 15 s while
  the stage is active and the app is open (the app IS open during
  alarms — the worker launched it). Retries stop on: ACK received,
  stage cancelled, or app exit (the DL channel then carries it).
- Companion dedup: `escalatedEpisodes` (SharedPreferences ring of the
  last 8 episode IDs). `escalate()` is a no-op for an already-
  escalated episode; `retract()` clears the entry so a re-fired
  episode after cancel is treated fresh (matches worker semantics —
  a cancelled episode never re-fires; a new one gets a new ID).

## D3. Heartbeat record v3 (16 bytes)

```
u32 epoch_s | u8 stage_det | u8 battery | u8 bpm | u8 suspended |
u16 change_age_s | u16 motion_age_s | u8 flags | u8 heap64 |
u16 episode
```
- `stage_det`: low nibble = cm_stage, high nibble = stage detector
  (0 when idle). Replaces the old pure-stage byte; v2 parsers that
  read it as a stage see stage values 0-3 unchanged when idle or when
  detector 0, and the companion is updated in the same release.
- Companion `DataLogReceiver` accepts 8/14/16; 14-byte records keep
  today's semantics (episode unknown → recovery falls back to
  "recover once per 10 min" time-dedup).
- **Recovery rule** (MonitorService, on worker heartbeat): if
  stage == ALARM and the episode is not in `escalatedEpisodes` → run
  the full alarm path (siren + AlarmActivity + server escalate) with
  the record's detector; mark the episode. COUNTDOWN is NOT recovered
  from the spool (owner decision 2026-08-29): records arrive minutes
  late, by which time a countdown is resolved or became an ALARM —
  only the latched, never-stale ALARM recovers from this channel.

## D4. Startup reconciliation + parked-action rules

- `WMSG_STATUS` repack: data0 = stage | (detector << 4) |
  (has_deadline << 8)... kept simple: data0 low byte = stage, high
  byte = detector; data1 = episode; data2 = seconds remaining in the
  current stage (0 if none). `last_bpm` moves out of STATUS (the diag
  path already carries it).
- App startup (and every `WMSG_STATUS` receipt): if worker stage is
  CHECKIN/COUNTDOWN/ALARM and no alert window is showing →
  reconstruct the alert UI for (stage, detector, episode) and send
  the corresponding PMSG to the phone (which dedups by episode).
- Parked-action expiry becomes type-aware: ladder actions
  (CHECKIN/COUNTDOWN/ALARM) never expire by age — reconciliation
  makes staleness harmless (worker state is the truth; a stale parked
  ALARM for a finished episode is dropped because status says
  stage=NONE... pickup consults last status, or simply: parked ladder
  actions are delivered, and the immediately-following status
  request corrects any staleness within 1 s). Informational nags keep
  the 60 s expiry.

## D5. Cancel ACK

- SELECT during a ladder stage: UI switches to "Cancelling…",
  `WMSG_USER_OK` result is checked; on false, retry at 500 ms up to
  4×. UI clears ONLY on the worker's `ALERT_CANCELLED` action (the
  existing echo, which now carries the episode). If no echo within
  3 s: show "CANCEL FAILED — hold SELECT to retry" and send
  `PMSG_CANCEL_FAILED`?— no new message: send nothing; the phone will
  still receive the ALARM if the ladder proceeds, which is the safe
  direction (false alarm over missed alarm).

## D6. Monotonic worker clock

- Worker keeps `s_mono_ms`, advanced by the 1 Hz tick handler
  (+1000/tick) and used by ALL cm_* feed calls. Accel batches and HR
  events between ticks reuse the latest `s_mono_ms` (1 s granularity
  is sufficient: the finest cross-call window is freefall→impact at
  1500 ms, and same-batch samples share one timestamp today anyway).
- Wall clock (`time_ms`) remains for: DL record epoch, drill
  timestamps, suspension persistence. `PK_SUSPEND_UNTIL` stays epoch
  seconds; `restore_suspension` converts to a mono-relative duration
  at load. A wall-clock jump therefore cannot stretch or shorten any
  detector interval by construction; suspensions keep wall-clock
  semantics deliberately (a 30-min suspension should end 30 wall
  minutes later even across a time sync — document this choice).
- Host tests: add a "wall-clock jump" scenario driving the worker
  pattern (mono feed) to lock the invariant, plus a countdown-length
  test asserting the fuse is honored to the second.

## D7. Firmware-gated quality metric (finding 7)

- The companion reads the connected watch's firmware version via PK2
  (`ConnectedWatch` info). The qmetric push (`PMSG_SET_QMETRIC 1`)
  is sent only when the version tag matches a dev build
  (`v9.9.9-dev` — the diag firmware); otherwise 0 is pushed even if
  the switch is on, and the Debug switch line shows "waiting for diag
  firmware". The worker keeps persisting the flag (fast arming after
  reboot) but the phone now actively pushes 0 on a stock-firmware
  connection, clearing a stale persisted 1 within one heartbeat.
  Residual window: worker boots on stock firmware with a stale flag
  before the phone connects — mitigated by the worker deferring the
  first metric-9 peek until 90 s after init (by then the phone's
  correction has landed via the heartbeat resync in every realistic
  sequence).

## Rejected alternatives

- **Guaranteed watch→phone delivery via DataLogging only** (drop the
  AppMessage ALARM): rejected — DL batches at ~4-6 min on current
  firmware; the live path must stay for latency, DL is the backup.
- **Random 32-bit episode IDs**: rejected — the persisted 16-bit
  sequence is collision-free by construction within the dedup window
  and fits existing message/record fields.
- **Monotonic clock inside the core** (SDK time source injection):
  rejected — the core is deliberately platform-free; the shell owns
  clock policy.
