# Tasks

- [x] 1. Core: episode identity — `episode_seq` in cm_core (shell
       seeds/persists PK_EPISODE_SEQ=12), minted on ladder start,
       `uint16_t episode` in cm_action, carried by
       CHECKIN/COUNTDOWN/ALARM/ALERT_CANCELLED. Host tests: distinct
       IDs across episodes, 0 for informational actions, cancel
       carries the episode it cancels.
- [x] 2. Worker: monotonic clock — `s_mono_ms` from the tick stream
       feeds all cm_* calls; suspension restore converts epoch →
       mono-relative; wall clock only for DL epoch/drill stamps.
       Host tests: wall-jump immunity + countdown-fuse-honored
       (design D6).
- [x] 3. Protocol: KEY_EPISODE=11, PMSG_ALARM_ACK=18, WMSG_STATUS
       repack (stage|detector, episode, stage-seconds-remaining),
       heartbeat record v3 (16 B: +episode u16; stage_det packed
       nibbles) — protocol.h + Protocol.kt mirrors.
- [x] 4. Watch app: acknowledged alarm sender — outbox sent/failed
       callbacks, pending-alarm slot, backoff retry until
       PMSG_ALARM_ACK / stage end (design D2).
- [x] 5. Watch app: cancel ACK — "Cancelling…" state, WMSG_USER_OK
       result checked + retried, UI clears only on ALERT_CANCELLED
       echo, loud failure otherwise (design D5).
- [x] 6. Watch app: startup/status reconciliation — reconstruct
       active-stage UI + PMSG from WMSG_STATUS; parked-action expiry
       becomes type-aware (ladder never expires by age).
- [x] 7. Companion: PMSG_ALARM_ACK reply, episode-dedup
       (`escalatedEpisodes` ring) in escalate/retract, DL v3 parse
       (8/14/16), COUNTDOWN/ALARM record recovery path with
       episode dedup (v2 falls back to 10-min time dedup).
- [x] 8. Companion: firmware-gated qmetric (design D7) — push
       SET_QMETRIC=1 only when the PK2-reported firmware version is a
       dev build; push 0 otherwise; Debug line reflects it. Worker
       defers the first metric-9 peek 90 s after init.
- [x] 9. Versions + builds: watchapp 0.5.0 (protocol-breaking rev),
       companion 0.6.0; dist; soak counters reset note.
- [ ] 10. Owner verification: alarm fires with Bluetooth OFF then ON
       (retry path); pull the watch out of range mid-countdown and
       confirm DL recovery escalates; cancel with the worker killed
       (build-update moment) shows the failure state; time-zone
       change mid-soak produces no detector event.
