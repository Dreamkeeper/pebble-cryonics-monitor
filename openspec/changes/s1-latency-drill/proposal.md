# Proposal: s1-latency-drill

## Why

M0 spike S1 (worker alarm-path latency, go-gate < 3 s) had no reliable
trigger: provoking a real detector event takes minutes of sitting still
and measures nothing precisely. The owner asked for a one-click trigger
in the admin panel and automated latency collection, reusable across
phone models.

## What changes

A **latency drill** that exercises the genuine cold alarm path —
`worker fires → persist handoff → worker_launch_app() → app up →
vibration → AppMessage to phone` — and records both halves:

- **Watch-precise launch latency**: the worker stamps its fire time
  (shared wall clock) into persist; the launched app computes
  `now - fire` in ms and both vibrates and reports it.
- **Phone-path estimate**: the phone times the whole round trip and
  subtracts the drill's fixed 10 s arming delay (includes BT transport
  both ways).

Triggers:
1. **Dashboard** (admin): "⏱ Latency drill" on the wearer page queues a
   command; the phone picks it up on its next heartbeat (≤5 min) — the
   first server→phone command channel (kv-backed, delivered exactly
   once).
2. **Android app** (Diagnostics): "Run watch latency drill" runs it
   immediately.

Results land in CmLog and as a `latency_drill` event on the server
(wearer Recent events + audit), tagged with `Build.MODEL` — so numbers
accumulate per phone model over time.

Drill sequence: phone opens the watchapp → app arms the worker and
exits → worker waits 10 s (so the launch is a true cold start) → fires
a synthetic `CM_ACT_LATENCY_DRILL` through `notify_app(launch=true)`
(the exact path real alarms take) → app relaunches, vibrates, shows and
sends the number.

## Impact

- Specs: watch-phone-protocol (ADDED requirement)
- Code: protocol.h/worker.c/main.c (PMSG 11/12, WMSG 9, PK 7),
  Protocol.kt/ServerClient.kt/MonitorService.kt/MainActivity.kt,
  server main.py/store.py/ui.py/wearer.html
- The detector core is untouched: CM_ACT_LATENCY_DRILL is emitted by
  the worker shell only, never by the core (deterministic alarm path
  stays pristine).
