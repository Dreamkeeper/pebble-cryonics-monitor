# Codebase review findings — 2026-08-29

Reviewed: `dc5e79706cc8f448defbe4989e72c45632022c76`

## Verdict

The detector core is compact and generally coherent; its 154 host checks pass, including the ladder, charging, suspension, sensor-lab, removal, and frozen-pulse scenarios. The watch shell is not yet safe enough for deployment, however: the only confirmed-alarm message is fire-and-forget, its DataLogging backup discards the alarm stage, and a delayed foreground launch can discard the parked action.

The Android companion has useful fail-loud UI and reboot machinery, and all 15 JVM tests pass. Its safety state is nevertheless too process-local, its worker-recovery path can strand itself after deferring to another watchapp, and the unprotected legacy broadcast surface lets another installed app forge watch state, inject an alarm, or crash the monitor process.

The server's tenant authentication, persistent escalation snapshots, CSRF-protected dashboard actions, and channel fan-out are substantially sound; all 69 server tests pass. Blockers remain around a state-changing GET acknowledgement, unsynchronized in-memory state shared between FastAPI worker threads and the pump, and an at-most-once command queue described as exactly-once.

The watchdog chain traced was: worker sensors -> parked worker action -> foreground watchapp -> AppMessage -> `MonitorService` -> `/api/v1/alarm` -> persistent pump -> responder channel/ACK. No component confirms the AppMessage leg; DataLogging does not recover an ALARM; before the first DataLogging record, no component proves the worker is alive; and the server receives `watch_data_age_s` but does not act on it. This is the principal unmonitored gap.

Verification performed:

- `watchapp/tests/test_detectors.c`: 154 checks, 0 failures (MSVC `/W4 /std:c11`).
- Android `testSideloadDebugUnitTest`: 15 tests, 0 failures.
- `server/tests`: 69 passed.

## Findings

### 1. [Missed alarm] The confirmed watch alarm is sent once, with no delivery acknowledgement or retry — CONFIRMED

Location: `watchapp/src/c/main.c:76-88`, `watchapp/src/c/main.c:275-280`, `android/app/src/main/java/org/cryomonitor/companion/DataLogReceiver.kt:93-103`, `android/app/src/main/java/org/cryomonitor/companion/MonitorService.kt:612-664`

Scenario: the ladder reaches ALARM while the AppMessage outbox is busy, Bluetooth drops briefly, or the asynchronous send is NACKed. `send_to_phone()` either returns immediately or ignores the eventual send result, and the latched worker state never re-emits that ALARM. Even if a later DataLogging heartbeat arrives with `stage == CM_STAGE_ALARM`, the receiver forwards `stage` but `MonitorService` ignores it. The phone siren, server escalation, and responders therefore see no alarm. Pebble's SDK-documented outbox-sent/outbox-failed callbacks exist for this asynchronous result, but the app registers neither.

Fix: give every ladder episode a durable event ID; persist an unsent ALARM in the watchapp; register outbox success/failure callbacks and retry with bounded backoff until the companion returns an application-level ACK. Treat a DataLogging record reporting COUNTDOWN/ALARM as an authoritative recovery path, deduplicated by the same event ID.

### 2. [Missed alarm] A mail/link scanner can acknowledge an escalation and stop tier promotion — PLAUSIBLE

Location: `server/app/main.py:228-246`, `server/app/escalation.py:137-155`, `server/app/channels.py:76-85`, `server/app/channels.py:100-110`

Scenario: an email security scanner, notification link preview, or other automated client prefetches the emailed/ntfy ACK URL. The unauthenticated GET immediately calls `record_ack()`. `any_ack` then becomes true and the next responder tier is never promoted, although no human has seen or accepted the incident. Automated link fetching is external behavior and was not reproduced in this review, hence PLAUSIBLE; the state mutation on GET is confirmed.

Fix: make GET render a no-side-effect confirmation page and record the ACK only on an explicit POST. For ntfy, use an explicit HTTP action that can POST. Bind the confirmation to the one-time token, consume it atomically, and retain Telegram callback ACKs as the genuinely one-tap path.

### 3. [Missed alarm] A delayed worker launch silently expires the only parked safety action — PLAUSIBLE

Location: `watchapp/worker_src/c/worker.c:126-135`, `watchapp/src/c/main.c:387-401`, `watchapp/src/c/main.c:455-489`

Scenario: `worker_launch_app()` is delayed for more than 60 seconds by a launch failure, repeated foreground crash, or watch lifecycle corner. The foreground app deletes and drops the parked action as stale. Its subsequent status request can learn that the worker is in COUNTDOWN or ALARM, but the status handler only updates/auto-closes UI; it never reconstructs the alert or informs the phone. The measured normal launch is much faster, so the triggering platform delay still needs a runtime fault-injection test.

Fix: expire informational actions, not active CHECKIN/COUNTDOWN/ALARM actions. Include detector, stage, episode ID, and stage start/deadline in `WMSG_STATUS`, and make foreground startup reconcile any active safety stage even when the parked transition record is absent.

### 4. [Missed alarm] Suspension auto-resume contradicts the source-of-truth specification and can remain blind after re-wear — CONFIRMED

Location: `openspec/specs/suspension/spec.md:33-48`, `watchapp/src/core/detectors.c:271-309`, `watchapp/tests/test_detectors.c:497-506`

Scenario: after a shower or swim, the wearer puts the watch back on and moves continuously, but the HR sensor is slow, failed, or produces no changing value. The main specification says accelerometer motion is the only trusted auto-resume signal; the implementation and tests require both motion and a changing pulse on HR hardware. Monitoring therefore remains suspended for the rest of the selected 30/60/120-minute window, during which a real event is ignored.

Fix: resolve the product decision explicitly. Either restore the specified motion-only resume and keep the existing timer-only Carry mode for off-wrist transport, or update the main spec and UI to state that normal suspension requires motion plus pulse and add a loud fault when re-wear motion is present but HR prevents resume. Do not leave safety behavior and the source-of-truth spec opposed.

### 5. [Stuck monitoring] Worker liveness can remain unmonitored indefinitely while the server reports the phone OK — CONFIRMED

Location: `android/app/src/main/java/org/cryomonitor/companion/MonitorService.kt:408-453`, `android/app/src/main/java/org/cryomonitor/companion/SettingsStore.kt:57-66`, `server/app/main.py:150-170`

Scenario: the Pebble worker is evicted before the companion has ever received a DataLogging record. `workerLastRecT > 0` is false, so the worker-silent watchdog never arms. If periodic sync is set to 0, this remains silent indefinitely; at its default it can remain blind for up to an hour. The Android service keeps sending healthy phone heartbeats. The server merely logs `watch_data_age_s` and keeps the wearer in phone state OK, so neither responders nor the dashboard learn that no watch detector is running.

Fix: make lack of any worker proof a DEGRADED/FAULT state after a bounded provisioning grace; persist the last worker proof across service restarts; require periodic challenge/response when DataLogging is unavailable; and have the server store and evaluate watch-link/worker age separately from phone liveness. Disabling periodic proof should produce a persistent safety warning, not an apparently healthy state.

### 6. [Stuck monitoring] Deferring self-heal to a workout can permanently suppress that recovery attempt — CONFIRMED

Location: `android/app/src/main/java/org/cryomonitor/companion/MonitorService.kt:427-440`, `android/app/src/main/java/org/cryomonitor/companion/MonitorService.kt:289-307`

Scenario: the worker-silent threshold is crossed while a workout app is active. The watchdog sets `workerFaultNotified = true`, and `selfHealLaunch()` sets its throttle timestamp before discovering the foreign app and returning. Future watchdog iterations do not call self-heal because `workerFaultNotified` remains true; a dead worker cannot produce the DataLogging record that clears it. The log says "retrying later", but no retry is scheduled for this one-shot fault.

Fix: make self-heal return a result and mark the fault handled only after a successful launch/worker proof. Queue a durable deferred recovery with bounded retries while the foreign app is active, preserve the visible fault throughout, and retry immediately when the watch returns to a watchface.

### 7. [Stuck monitoring] A persisted diagnostic-firmware flag can crash the worker after a firmware downgrade — PLAUSIBLE

Location: `watchapp/worker_src/c/worker.c:241-267`, `watchapp/worker_src/c/worker.c:407-410`, `watchapp/worker_src/c/worker.c:445-457`, `android/app/src/main/java/org/cryomonitor/companion/SettingsStore.kt:47-55`

Scenario: raw quality mode is enabled on the diagnostic Time 2 firmware, setting `PK_QMETRIC=1`, and the watch is later returned to stock firmware. The persisted flag is restored before any per-boot capability handshake, and every HR event calls undocumented metric 9. The repository's own field note says stock firmware asserts on that metric, so the worker can crash on every restart. This firmware behavior was not reproduced during the review, hence PLAUSIBLE.

Fix: never persist a bare firmware capability across boots. Bind it to a verified firmware build identifier and clear it before worker startup unless the foreground app proves support for the current boot; ideally expose a supported capability API instead of peeking an unknown enum value.

### 8. [False alarm to contacts] Alarm correlation is volatile and non-idempotent, so cancel can leave an escalation running — CONFIRMED

Location: `android/app/src/main/java/org/cryomonitor/companion/MonitorService.kt:51`, `android/app/src/main/java/org/cryomonitor/companion/MonitorService.kt:321-340`, `server/app/main.py:96-112`, `server/app/main.py:198-204`

Scenario: the server commits an alarm but the HTTP response is lost, or `MonitorService` is killed after receiving the escalation ID. The server has an active escalation, but `activeEscalationId` is null after failure/restart. A later watch cancellation calls `retract()` and resolves nothing, while the server keeps repeating/promoting to contacts. A duplicate watch message similarly creates a second UUID escalation and overwrites the one in memory, leaving the earlier one active.

Fix: generate and persist an alarm episode/idempotency ID before sending; make `/alarm` an idempotent upsert keyed by wearer plus episode; persist all unresolved IDs on the phone; and reconcile them from `/status` on every service start and before cancellation. Resolve by episode, not by one volatile response value.

### 9. [False alarm to contacts] Watch-side cancellation hides the UI before the worker confirms it — CONFIRMED

Location: `watchapp/src/c/main.c:225-230`, `watchapp/src/c/main.c:406-413`, `watchapp/worker_src/c/worker.c:374-416`

Scenario: the wearer presses SELECT during CHECKIN/COUNTDOWN while worker IPC is unavailable, full, or racing a worker restart. `app_worker_send_message()` is unchecked and the alert window is removed immediately. The worker never receives `WMSG_USER_OK`, continues to ALARM, and contacts are alerted after the wearer was shown a successful-looking cancellation.

Fix: add a worker-to-app cancellation acknowledgement carrying the episode ID. Keep a clear "Cancelling…" safety UI and retry until acknowledged; if the worker cannot be reached, fail loudly and ask the companion to cancel/escalate the fault rather than silently dismissing the screen.

### 10. [False alarm to contacts] Dead-man evaluation races a concurrent heartbeat — PLAUSIBLE

Location: `server/app/main.py:150-170`, `server/app/main.py:335-360`, `server/app/deadman.py:40-45`, `server/app/deadman.py:59-79`

Scenario: the async pump evaluates a wearer as SILENT just as FastAPI executes the synchronous heartbeat endpoint in its worker thread. The heartbeat changes the same in-memory `DeadmanMonitor` to OK, but the pump retains its local SILENT result and creates a `phone_silent` escalation anyway. On the next cycle it may send a recovery, but the false advisory has already reached contacts. The necessary interleaving was not forced in tests, hence PLAUSIBLE.

Fix: serialize each wearer's heartbeat/evaluate/transition/create sequence under one lock or single event-loop queue. Re-read the persisted heartbeat baseline immediately before creating a silence escalation, and add a deterministic concurrency test around the threshold.

### 11. [False alarm to contacts] Wall-clock corrections are interpreted as huge elapsed detector intervals — PLAUSIBLE

Location: `watchapp/worker_src/c/worker.c:72-81`, `watchapp/src/core/detectors.c:4-5`, `watchapp/src/core/detectors.c:314-325`

Scenario: Pebble wall time is corrected forward or backward while COUNTDOWN is active. All detector ages use truncated epoch milliseconds; unsigned subtraction turns a backward correction into an interval near 2^32 ms, and a forward correction can simply exceed the fuse. The next tick can latch ALARM before the promised countdown has elapsed. This review did not inject a watch time correction; the risk is inferred from `time_ms`/`time_t` wall-clock semantics, not assumed monotonic behavior.

Fix: drive detector and ladder intervals from a monotonic worker tick counter. Use wall clock only for display and persisted suspension conversion, with explicit jump detection that resets safe baselines but never shortens an active countdown.

### 12. [Security/privacy] Unprotected exported legacy broadcasts permit alarm injection, watchdog forgery, and process crashes — CONFIRMED

Location: `android/app/src/main/AndroidManifest.xml:102-112`, `android/app/src/main/java/org/cryomonitor/companion/MonitorService.kt:68-87`, `android/app/src/main/java/org/cryomonitor/companion/PebbleTransport.kt:32-52`, `android/app/src/main/java/org/cryomonitor/companion/PebbleTransport.kt:56-67`, `android/app/src/main/java/org/cryomonitor/companion/PebbleTransport.kt:108-121`, `android/app/src/main/java/org/cryomonitor/companion/DataLogReceiver.kt:27-50`

Scenario: any installed Android app sends `com.getpebble.action.app.RECEIVE` with the public watch UUID and `MSG_TYPE=ALARM`, causing real server/contact escalation; sends malformed JSON, causing an uncaught parser exception in the monitor process; or repeatedly sends DataLogging broadcasts with no UUID (explicitly accepted) to refresh `workerLastRecT` and mask a dead worker. Android-documented exported receiver behavior permits matching broadcasts from other apps; no sender permission, UID verification, MAC, or robust exception boundary is present.

Fix: remove the legacy transport when PK2 is available, or protect it with a capability the Core app can satisfy: explicit package/UID verification plus a cryptographic per-install challenge/MAC for payloads. Require the exact UUID and session identity, parse all hostile input inside a no-throw boundary, deduplicate by session plus data ID, and never let an untrusted heartbeat reset a safety watchdog.

### 13. [Security/privacy] Wearer-scoped status leaks global operator audit data — CONFIRMED

Location: `server/app/store.py:331-340`, `server/app/main.py:274-286`, `server/app/operators.py:92-109`

Scenario: any wearer bearer token calls `/api/v1/status`. `recent_events(wearer_id)` deliberately includes rows where `wearer_id IS NULL`, so the response can contain global `login_failed`, login, operator creation/role, and other administrative events, including operator usernames and client addresses. This violates the server's own tenant/exposure requirements even though it does not reveal another wearer's rows.

Fix: for wearer authorization, query exactly `WHERE wearer_id=?`. Reserve NULL/global audit rows for authenticated operators; if wearers need system-wide notices, store a separately typed and explicitly safe announcement stream.

### 14. [Data loss/corruption] The command queue deletes work before the phone acknowledges receipt — CONFIRMED

Location: `server/app/main.py:164-170`, `server/app/store.py:431-446`

Scenario: a queued latency drill is selected and deleted inside the heartbeat transaction, then the HTTP response is lost before reaching the phone. The retrying heartbeat finds no command, so the drill disappears. The existing "exactly once" test covers two successful requests, not this response-loss boundary.

Fix: assign durable command IDs and states (`pending`, `leased`, `acknowledged`); return a lease repeatedly until the phone includes the command ID in a later heartbeat/result ACK. Make command execution idempotent on the phone.

### 15. [Everything else] Starting/restarting the service can still steal the watch screen during workout startup — CONFIRMED

Location: `android/app/src/main/java/org/cryomonitor/companion/MonitorService.kt:57-89`, `android/app/src/main/java/org/cryomonitor/companion/MonitorService.kt:289-307`, `android/app/src/main/java/org/cryomonitor/companion/WatchLink.kt:45-59`

Scenario: every fresh service creation calls `selfHealLaunch("service start")`. During the observed Core workout transition, the active-app query can run before the workout becomes authoritative and report no foreign app; the monitor then starts its own watchapp and closes/disrupts the workout. This matches the reported field behavior and violates the protocol requirement that only attention-demanding events take the screen.

Fix: remove unconditional service-start launches. Start the watchapp only for an explicit user test/sync or a verified worker-silent fault, defer until active-app state is stable for a short window, and record the launch reason so a regression test can assert that workout/app-switch provider churn never launches Cryonics Monitor.
