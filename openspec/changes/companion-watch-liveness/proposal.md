# Proposal: companion-watch-liveness

## Why

In worker mode the phone only hears the watch while the watchapp is
open: the worker cannot use AppMessage. Between app launches the
"synced" age grows unboundedly, worker eviction is undetectable, and
the server's watch_data_age is fiction. M0 spike S5's answer: the
worker's DataLogging session — records the worker has been writing
every 60 s since v0.1 — just needed a receiver on the phone.

## What changes

- **DataLogReceiver** (Android): implements the legacy PebbleKit
  DataLogging broadcast protocol (RECEIVE_DATA / ACK_DATA /
  REQUEST_DATA / FINISH_SESSION) — PebbleKit2 1.2.0 has no DataLogging
  API (verified against the library binaries). Parses the 8-byte
  cm_heartbeat_rec (epoch, stage, battery, bpm, suspended), ACKs every
  record, delivers into MonitorService.
- **Liveness accounting**: each record refreshes lastWatchDataT (the
  "synced" age becomes honest without opening the watchapp), updates
  the watch battery, and logs the flush latency + running median — the
  S5 go-gate number (median < 60 s) reads straight from the app logs.
- **Worker-eviction watchdog**: once records have ever flowed, their
  silence (> 10 min) while the BT link is up raises a FAULT and
  relaunches the watchapp to re-arm the worker. Armed only after first
  contact, so a Core app that never emits the legacy broadcasts stays
  quiet instead of crying wolf.
- **Flush requests**: the phone broadcasts REQUEST_DATA once a minute.

## The experiment inside the feature

Whether the Core Devices app emits the legacy DataLogging broadcasts is
unconfirmed — that is S5. Every path logs loudly; one field run
decides. If nothing arrives: the receiver stays inert (no faults, no
regressions) and S5 falls back to the hourly worker status sync.

## Impact

- Specs: watch-phone-protocol (ADDED requirement)
- Code: Android only (DataLogReceiver.kt, MonitorService, Protocol.kt,
  manifest). Watch side already ships (worker tag 0xC201, 60 s cadence).
