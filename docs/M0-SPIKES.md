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

## S2 — Worker survival across reboot / battery death — ✅ GO (field 2026-08-18)
**Question:** Does the background worker auto-restart after a watch reboot or
battery die→recharge cycle? (Undocumented.)
**Result:** owner reboot test: Cryonics Monitor **remains the selected
Background App after a watch reboot** — the worker slot survives. The
"Go" criterion allowed either auto-restart or companion-driven
relaunch; since companion ≥ v0.3.6 the phone relaunches the watchapp
(and therefore the worker) automatically on every watch reconnect
(reboot included), so the ladder is re-armed either way without wearer
action. Battery-death→recharge variant: same mechanism applies on the
post-charge reconnect; verify opportunistically during the next natural
battery cycle. Residual dependency: the phone must be present at
reconnect — already covered by the dead-man advisory if it is not.

## S3 — Worker vibration capability — ✅ NO (build-proven 2026-08-18)
**Question:** Can the worker call `vibes_*` directly?
**Method + result:** added `vibes_short_pulse()` to `worker_init` and
built: `error: implicit declaration of function 'vibes_short_pulse'` —
the symbol is compiled out of the worker API (SDK 4.9.169). The worker
genuinely cannot vibrate; `worker_launch_app()` + foreground vibe is the
only alarm path. Architecture assumption confirmed; no change needed.

## S4 — Raw HR behavior at 1 s period — ✅ ANSWERED (guided lab, 2026-08-27)

**Result (Time 2, full lab run):** worn stages read 100 % nonzero with
live jitter (moving 68–88, still 74–81, loose strap still tracks
75–80). **Off-body, the firmware never reports "no reading": it serves
the LAST computed bpm with fresh events — bit-identical (82) for 9+
minutes across table-flat, face-down, and fabric.** Event cadence stays
~1 Hz in burst. Burst spin-up after a period switch: first event ~23 s.
**Consequences (implemented in wear-detection-tuning round 2): pulse
LIVENESS = a CHANGING value.** A frozen feed triggers the hunt
(`pulse_flat_after_s` 300 s); only a changed value ends hunts or
dismisses pulse check-ins; removal discrimination, non-motion
proof-of-life, and the not-worn nag key on the change timestamp;
`pulse_hunt_s` raised to 45 s for the spin-up lag. Original go
criterion (off-wrist distinguishable within 60 s of signal+stillness)
is NOT met by signal presence alone — it IS met by signal *flatness*,
within `pulse_flat_after_s`.
**To re-run** (new watch, new strap, firmware update): Android app →
Debug & feasibility tests → **Start sensor lab** — guided,
confirmation-gated stages, recorded and shareable.

## S5 — DataLogging heartbeat latency — 🟡 receiver shipped, run the field hour
**Question:** When BT-connected, how quickly does a DataLogging session
flush to Android? Is PebbleKit2's DataLogging support functional?
**State:** BOTH halves shipped. Watch: 8-byte `cm_heartbeat_rec` every
60 s (tag 0xC201) since v0.1. Phone (companion ≥ v0.3.8): a legacy-
protocol DataLogging receiver (PK2 1.2.0 has none — verified against
the library binaries) that ACKs records, refreshes the "synced" age and
watch battery, logs per-record flush latency + running median, and
raises a worker-eviction FAULT if records ever flow and then stop.
**Method:** wear the watch ≥1 h with the watchapp CLOSED. Then Android
app → View logs: `WORKER HEARTBEAT via DataLogging ... flush-latency=Ns`
and `S5: ... median=Ns`. The notification's `synced` age staying fresh
with the app closed is the visible pass. **No records at all after an
hour** = the Core app does not emit the legacy broadcasts → NO-GO,
fall back to the hourly worker status sync.
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

## S8 — Worker memory headroom — ✅ GO, thin margin (lab, 2026-08-27)

**Result:** 2112 B free throughout a full sensor lab — burst HR +
accel + detector state + lab relay, the worst case measured — against
the > 2048 B gate. **GO by 64 bytes**: any future worker feature must
re-check this number (the debug line shows it live).
**Question:** Actual free heap in the worker on emery after subscribing to
accel (25-sample batch) + HealthService events (~2 kB) + detector state.
**Method:** debug ON, watchapp open → `heap <bytes>` on the debug line
(live from the worker, 64-byte resolution). Also logged at worker start
(`worker up … heap_free=`) and per minute via `pebble logs`. Check it
(1) right after boot, (2) after a day of wear, (3) during a pulse hunt
(burst subscription active) — the minimum of the three is the answer.
**Go:** > 2 kB headroom remaining.
**Result:** _pending_
