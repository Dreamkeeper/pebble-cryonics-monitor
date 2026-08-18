# M0 Feasibility Spikes — go/no-go gates

Each spike answers a question the platform docs leave open. Run on real
hardware (Pebble Time 2 primary). Record results in this file.

Status legend: ✅ answered · 🟡 instrumented, needs a field run · 🔴 needs
spike code first.

Instrumentation shipped for these spikes (watchapp ≥ v0.3.5, server ≥
0.3.x schema v3):

- **On-watch telemetry**: Debug mode ON (Android app → Diagnostics
  toggle, pushes to the watch) → open the watchapp → the bottom line
  shows `st<stage> susp<min> DBG / bpm <raw> · heap <bytes>`, refreshed
  from the worker every few seconds. bpm is the last raw HR reading
  (0 = no signal), heap is free worker heap.
- **Worker logs** (deeper): every HR update (`hr raw=… burst=…`) and a
  per-minute line (`min: … mag=… bpm=… hr_upd=… heap_free=…`). Read via
  the developer connection: enable it in the Core app, then from WSL
  `pebble logs --phone <phone-ip>`.
- **Watch battery trail**: the server now stores watch battery with
  every phone heartbeat (48 h window). Hover the dashboard battery bars
  ("phone X% · watch Y%"), or pull raw numbers on the NAS:
  `docker exec cryomonitor-monitor-1 python3 -c "import sqlite3;
  [print(r) for r in sqlite3.connect('/srv/data/cryomonitor.db')
  .execute('SELECT datetime(t,\"unixepoch\"), watch_battery FROM
  heartbeat_trail WHERE watch_battery IS NOT NULL ORDER BY t')]"`
- **Android logs**: Debug mode → View logs; every PebbleKit2 receive is
  timestamped to the millisecond.

## S1 — Worker alarm path latency — ✅ GO (measured 2026-08-18)

**Result:** first drill on the Time 2 + Xiaomi 17 Ultra (25128PNA1C):
**worker→app cold launch = 71 ms** — two orders of magnitude under the
3 s gate. Vibration follows within the same handler. (The first run's
"phone-path −482 ms" exposed an arithmetic bug — the phone subtracted a
guessed 10 s countdown, but the worker's timer is tick-aligned; since
v0.3.7 the watch reports its own arm→result total and the phone derives
pure BT transport instead.) Keep running the drill on new phone models;
results accumulate as `latency_drill` events per model.
**Question:** How long from `worker_launch_app()` in the background worker to
(a) first vibration and (b) AppMessage delivered to the Android companion?
**How to re-run — automated latency drill** (watchapp ≥ v0.3.6,
companion ≥ v0.3.4): press **"Run watch latency drill"** in the Android
app (Diagnostics), or **"⏱ Latency drill"** on the wearer's dashboard
page (queued; the phone picks it up on its next heartbeat, ≤5 min).
Sequence: the watchapp opens, closes itself, and ~10 s later the worker
fires a synthetic alert through the REAL cold alarm path — the watch
relaunches, buzzes, and shows `launch N ms`. Recorded per run in the
wearer's Recent events, tagged with the phone model:
- `launch_ms` — worker fire → app alive, watch-clock precise;
- `watch_ms` — arm → result handoff, watch-clock;
- `rtt_ms` / `transport_ms` — phone round trip, and rtt − watch_ms =
  pure BT transport both ways.
**Go:** < 3 s end-to-end — **met** (71 ms + sub-second transport).

## S2 — Worker survival across reboot / battery death — 🟡 10-min manual run
**Question:** Does the background worker auto-restart after a watch reboot or
battery die→recharge cycle? (Undocumented.)
**Method:** With the phone notification showing `watch ✓`: reboot the
watch (long-hold BACK, or Settings → System); after the watchface
returns check Core app → Settings → Background App still lists Cryonics
Monitor, open the watchapp once, confirm fresh `synced` age on the
phone. Repeat overnight with a full battery drain → recharge.
**Go:** auto-restarts, or companion can detect death and re-launch via app start.
**Safety note:** the companion already detects a dead watch link
(`watch LINK DOWN` + FAULT); opening the watchapp relaunches the worker.
**Result:** _pending_

## S3 — Worker vibration capability — ✅ NO (build-proven 2026-08-18)
**Question:** Can the worker call `vibes_*` directly?
**Method + result:** added `vibes_short_pulse()` to `worker_init` and
built: `error: implicit declaration of function 'vibes_short_pulse'` —
the symbol is compiled out of the worker API (SDK 4.9.169). The worker
genuinely cannot vibrate; `worker_launch_app()` + foreground vibe is the
only alarm path. Architecture assumption confirmed; no change needed.

## S4 — Raw HR behavior at 1 s period — 🟡 instrumented, run the 4×5-min matrix
**Question:** What does `HealthMetricHeartRateRawBPM` report (values +
update cadence) when: worn normally; worn but perfectly still; strap
loose; watch on a table? Does the system honor ~1 s in burst or throttle?
**Why it matters now:** the wear-detection rework assumes off-body ≈ "no
valid reading"; the T4 field observation (instant "pulse" with the
sensor against a surface) suggests phantom readings — this spike
confirms or kills that.
**Method:** debug ON, watchapp open, read the `bpm` line for ~5 min per
condition: (1) worn, moving; (2) worn, dead still; (3) strap loose /
watch resting on the palm; (4) on the table — flat, face-down, and
pressed against fabric. Record value + how often it changes. For burst
cadence: provoke a pulse hunt (condition 4 after wearing) and read
`hr raw=… burst=1` lines via `pebble logs`.
**Go:** off-wrist/still states distinguishable from normal wear within 60 s
using HR-signal-presence + accel variance together.
**Result:** _pending_ (field hint 2026-08-18: watch on table appeared to
keep a "pulse" — phantom readings likely; quantify here)

## S5 — DataLogging heartbeat latency — 🔴 needs the Android receiver
**Question:** When BT-connected, how quickly does a DataLogging session
flush to Android? Is PebbleKit2's DataLogging support functional?
**State:** the worker already logs a 8-byte `cm_heartbeat_rec` (epoch,
stage, battery, last_bpm, suspended) every 60 s into a DataLogging
session — the watch half of the spike is running in production today.
The Android half (a `PebbleDataLogReceiver`, possibly via the Classic
API alongside PK2 — that coexistence IS part of the question) does not
exist yet. Propose as the next OpenSpec change: `companion-watch-liveness`.
**Method once built:** compare `rec.epoch_s` to Android receive time over
an hour of normal wear; median + p95.
**Go:** median flush < 60 s → worker heartbeat channel; kills the
"synced 22443s" ambiguity for good (M0-S5 note in MonitorService).
**No-go fallback:** periodic `worker_launch_app()` status sync (hourly) +
companion treats AppMessage silence as the heartbeat signal.
**Result:** _pending_

## S6 — Battery drain per sampling profile — 🟡 trail recording live
**Question:** %/hour at: (a) accel 25 Hz batched 25, HR off; (b) + HR 60 s
period; (c) + HR 1 s period (burst). Worker mode.
**Method:** profile (b) is today's default — just wear the watch for
24–48 h and read the watch-battery trail (dashboard bar tooltips, or
the sqlite one-liner above). Pebble reports battery in 10 % steps, so
use ≥ 24 h per estimate. (a)/(c) need config toggles — only worth
building if (b) fails its gate.
**Go:** profile (b) ≥ 7 days projected battery on Time 2.
**Result:** _pending_ (trail starts populating from server 0.3.x deploy,
2026-08-18)

## S7 — PebbleKitAndroid2 end-to-end — ✅ PASS (field-proven since 2026-08-14)
**Question:** AppMessage round-trip watch↔phone with the Core app;
`companionApp` pairing; connection events.
**Evidence:** PK2 (io.rebble.pebblekit2:client:1.2.0) with the
`companionApp` object in package.json is the production transport:
enrollment config push, alarm delivery, ACK round trips, live status
polls, and E2E T1/T2 all ran through it on the Time 2 + Xiaomi 17 Ultra.
Classic broadcast-intent fallback retained but unused. DataLogging
support remains unverified → S5.

## S8 — Worker memory headroom — 🟡 read it off the watch
**Question:** Actual free heap in the worker on emery after subscribing to
accel (25-sample batch) + HealthService events (~2 kB) + detector state.
**Method:** debug ON, watchapp open → `heap <bytes>` on the debug line
(live from the worker, 64-byte resolution). Also logged at worker start
(`worker up … heap_free=`) and per minute via `pebble logs`. Check it
(1) right after boot, (2) after a day of wear, (3) during a pulse hunt
(burst subscription active) — the minimum of the three is the answer.
**Go:** > 2 kB headroom remaining.
**Result:** _pending_
