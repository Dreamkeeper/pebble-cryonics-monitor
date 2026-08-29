# Triage of the codebase adversarial review

Findings: [CODEBASE-REVIEW-FINDINGS.md](CODEBASE-REVIEW-FINDINGS.md)
(Codex, 2026-08-29). Verdict on the review: high quality — the
severity ranking matches the product's stakes, and the confirmed
findings traced to real code. Every finding was checked against
source. Summary: **9 fixed now, 6 deferred with a plan** (the deferred
ones need coordinated watch↔phone protocol changes — episode IDs,
ACKs, a monotonic clock — that belong in their own OpenSpec change,
not a quick patch).

Versions: watchapp 0.4.13, companion 0.5.9, server redeployed.

## Fixed now

| # | Sev | Finding | Fix |
|---|-----|---------|-----|
| 5 | Stuck monitoring | Worker liveness unmonitored before first DL record / with sync=0 | New provisioning-liveness watchdog: link up but **no** worker proof (DL record or open-app message) within a 15-min grace → FAULT + self-heal. Open-app messages now also count as worker proof. |
| 6 | Stuck monitoring | Self-heal deferred to a workout never retried; fault stuck true | `selfHealLaunch` now DEFERS (not drops) behind a foreign app via a `selfHealPending` flag the watchdog retries each tick once a watchface returns; the fault stays visible until a launch truly fires. |
| 8 | False alarm | Volatile alarm id + non-idempotent cancel leaves escalations running | Server `/alarm` is now idempotent (returns the existing open escalation for the same wearer+kind+detector); new `/alarm/resolve-open` sweeps all open watch alarms by wearer; the phone's `retract()` calls it as a safety net even when the id was lost. |
| 10 | False alarm | Dead-man evaluate races a concurrent heartbeat → false advisory | A `threading.Lock` serializes each wearer's heartbeat mutation against the pump's evaluate→decide→create sequence. |
| 12 | Security | Malformed exported broadcast can crash the monitor process | The classic `INTENT_APP_RECEIVE` handler now parses UUID and JSON inside a no-throw boundary (wrong-UUID and unparseable payloads are dropped, not fatal). DataLogReceiver already required the exact UUID + dataId dedup + guarded decode. |
| 13 | Security | Wearer status leaked global operator audit rows (logins, roles) | `recent_events(wearer_id)` now queries `WHERE wearer_id=?` only; NULL/global audit rows are operator-scoped. |
| 14 | Data loss | Command deleted before the phone acknowledged receipt | `pop_command` → `peek_command` + `ack_command`: the command is leased (redelivered every heartbeat) until the phone echoes `command_ack`; the phone sends it on the next beat. |
| 15 | Screen-steal | Unconditional service-start self-heal steals the watch screen mid-workout | Removed the `selfHealLaunch("service start")` on every service creation. The watchapp launches only for an explicit user test/sync or a verified worker-silent/never-seen fault, each re-checking `foreignAppActive` at fire time. |
| 2 | Missed alarm | A link scanner can acknowledge an escalation via the GET ack URL | Ack GET now renders a no-side-effect confirm page; the ACK is recorded only on POST. ntfy uses an `http`/POST action; Telegram callback ACK unchanged. |

## Deferred with a plan (need a protocol change — next OpenSpec change `alarm-delivery-hardening`)

| # | Sev | Finding | Why deferred / plan |
|---|-----|---------|---------------------|
| 1 | Missed alarm | Confirmed ALARM is one-shot AppMessage, no ACK/retry; DataLogging doesn't recover an ALARM | The real fix is a durable **episode ID** + outbox success/failure callbacks + app-level ACK + treating a DL record reporting COUNTDOWN/ALARM as an authoritative recovery path. That is a coordinated watch+phone protocol addition (new message fields, worker-side persistence, phone dedup). Highest-value deferred item; leads the new change. Interim: the link-down and worker-silent watchdogs already surface a lost link as a FAULT within minutes. |
| 3 | Missed alarm | A >60 s delayed worker launch expires the only parked safety action | Same protocol change: carry detector+stage+episode ID+deadline in `WMSG_STATUS` and reconcile an active safety stage on foreground startup even with no parked record. Pairs with #1's episode ID. |
| 9 | False alarm | Watch cancel hides UI before the worker confirms it | Needs a worker→app cancellation ACK carrying the episode ID + a "Cancelling…" retry UI. Same protocol change (#1/#3 family). |
| 11 | False alarm | Wall-clock corrections misread as huge detector intervals; can shorten a countdown | The detector core keys all ages off truncated epoch-ms (`now_ms`). Fix is to drive ladder/detector intervals from a **monotonic** tick counter and use wall-clock only for display + persisted-suspension conversion, with explicit jump detection. A focused change to `detectors.c` + the worker clock source; touches the 154-test core, so it gets its own change with added time-jump tests. Confirmed real but no field trigger yet. |
| 7 | Stuck monitoring | Persisted `PK_QMETRIC` can crash the worker after a firmware downgrade | Proper fix: the companion should only enable the quality gate after verifying the running firmware is the diag build (PK2 firmware version), not from a bare persisted flag — this pairs with the eventual upstream quality-metric API. Interim mitigation (documented): turn the "Raw HR quality metric" switch OFF before downgrading firmware. Risk is limited to the diag-firmware developer doing a downgrade. |
| 12-crypto | Security | Any app can inject a forged ALARM AppMessage / forge worker heartbeats | The crash surface is fixed (above). Injection prevention needs a per-install challenge/MAC the Core app can satisfy, or migration off the classic broadcast to PK2-only once its DataLogging API exists — the same upstream work as mobileapp#386. Threat is bounded (attacker needs the app installed on the wearer's own phone and the public UUID); tracked for the PK2 migration. |

## Verification after fixes

- Server: 73 pytest pass (added: idempotent alarm, resolve-open, command lease/ack, wearer-audit isolation, ack GET-is-inert/POST-acks).
- Companion: 15 JVM tests pass; full compile clean.
- Detector core: unchanged this round (findings 4/11 touch it and are handled/deferred) — 154 checks still green.
- Finding 4 (auto-resume vs spec): CONFIRMED and now fixed. The
  motion+pulse implementation shipped in `sensor-fault-and-wrist-resume`,
  but that change's corrected spec lived only in its delta — the change
  was un-archived, so the living `suspension` spec still carried the old
  "motion only / pulse SHALL NOT resume" text the reviewer found. Fixed
  by archiving the change, which applied the delta: the living spec now
  states motion+pulse with Carry mode (timer-only) for off-wrist
  transport. Lesson: archive a change once its implementation ships, or
  the source-of-truth spec drifts from the code.
