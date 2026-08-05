# Pebble Cryonics Monitor

A 24/7 personal alarm system for cryonicists, built on Pebble smartwatches
(Pebble Time 2, Pebble 2 HR, Pebble 2 Duo, Pebble Round 2), an Android
companion app, and an optional self-hosted server.

It detects **unresponsiveness** — loss of pulse signal combined with
stillness, hard impacts (falls, vehicle crashes) followed by immobility,
prolonged absence of micro-movement, or a missed check-in — and escalates
through a configurable chain: the wearer first (multi-stage confirmations to
kill false positives), then relatives, then the cryonics organization's
standby team. Humans decide whether to call emergency services; the system
never auto-dials them.

> **This is not a medical device.** It does not diagnose cardiac arrest or
> any medical condition. Optical wrist sensors lose signal both when
> perfusion stops *and* when the strap is loose — the multi-stage
> confirmation ladder exists precisely because the two are indistinguishable
> at the sensor. Treat it as a personal alarm, not a monitor of record.

## Components

| Directory | What it is |
|---|---|
| [`watchapp/`](watchapp/) | Pebble app (C, SDK 4.x): background-worker or persistent-foreground monitoring, on-watch alert ladder, suspension menu. Targets `emery`, `diorite`, `flint`, `gabbro`. |
| [`android/`](android/) | Android companion (Kotlin): foreground service, watch watchdog, full-screen alarm with cancel window, phone-direct SMS/call/Telegram fallback escalation, server link. |
| [`server/`](server/) | Self-hosted backend (Python/FastAPI, Docker): phone dead-man monitoring, tiered escalation with delivery ACK + retry via Telegram/email/ntfy, web dashboard, learning-mode pattern miner. |
| [`docs/`](docs/) | Product plan, threshold rationale, M0 feasibility-spike checklist. |
| [`tools/`](tools/) | False-alarm log analysis, battery test harnesses. |

## Design lineage

- Alarm state machine, watchdog and escalation patterns ported from
  [OpenSeizureDetector](https://github.com/OpenSeizureDetector) (GPL-3.0).
- Alert-ladder UX modeled on Google Pixel Watch Loss of Pulse Detection
  (FDA De Novo) and Apple Watch Fall/Crash Detection.
- Detection thresholds informed by [cryonicsmonitoring.org](https://www.cryonicsmonitoring.org)
  and the Cryonics Institute Check-In escalation ladder.
- Foreground watchface mode incorporates rendering from
  [YaForecasWatch2](https://github.com/Dreamkeeper/YaForecasWatch2) (GPL-3.0).

## Status

**v0.1 pre-alpha** — builds end-to-end, on-hardware validation pending. See
[docs/M0-SPIKES.md](docs/M0-SPIKES.md) for the feasibility gates that must
pass on real hardware before the architecture is final.

## Building

**Watchapp** (pebble-tool 5.x + SDK 4.9.169+, Linux/WSL):

```bash
cd watchapp && pebble build     # -> build/watchapp.pbw (emery/diorite/flint/gabbro)
```

Install: sideload `build/watchapp.pbw` via the Pebble/Core mobile app or
Rebble Sideload Helper.

**Android companion** (JDK 17 + Android SDK 35):

```bash
cd android && gradle assembleSideloadDebug
# -> app/build/outputs/apk/sideload/debug/app-sideload-debug.apk
```

The `sideload` flavor includes SMS fallback escalation; the `play` flavor
omits SMS/call permissions for Play Store compliance.

**Server** (Docker; see [docs/DEPLOY-SYNOLOGY.md](docs/DEPLOY-SYNOLOGY.md)
for Synology Container Manager):

```bash
cd server && cp .env.example .env   # set CM_API_TOKEN etc.
docker compose up -d --build        # API on :8080, ntfy on :8090
```

Tests: `watchapp/tests` (host C, MSVC/gcc — 80 checks) and
`server/tests` (pytest — 17 checks).

## License

GPL-3.0 — see [LICENSE](LICENSE).
