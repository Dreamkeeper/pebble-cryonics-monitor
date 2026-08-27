# Proposal: s4-sensor-lab

## Why

M0 spike S4 (raw-HR behavior per wear condition) required the owner to
stare at a debug line with a stopwatch. The wear-detection logic leans
on this spike's answer (does the sensor phantom-read off-wrist?), so it
deserves an automated, repeatable, shareable test — and a debug home:
all debug/test tooling now lives in one Debug screen instead of
cluttering the main screen.

## What changes

- **Guided S4 sensor lab** (Android Debug screen): a ~13-minute staged
  sequence (worn-moving, worn-still, strap-loose, table-flat,
  face-down, fabric). The phone narrates each stage, vibrates at
  transitions, records every sample, then produces a per-stage summary
  (n, nonzero %, bpm min/median/max, median event age) + full CSV, with
  a Share button. Repeatable on any phone/watch pair.
- **Watch lab mode**: PMSG_HR_LAB / WMSG_HR_LAB switch the worker to
  1 s burst sampling and a **silent detector hold** (new core
  `cm_set_lab_hold`, same rules as the charging hold — the deliberate
  off-wrist stages must not start ladders; a latched ALARM survives).
  Every 2 s the worker relays raw peek bpm + seconds-since-last-HR-event
  + free heap (WMSG_HR_SAMPLE → PMSG_HR_SAMPLE); the watch shows the
  live numbers, and the auto-launch guard is held for the lab duration.
- **Debug screen** consolidates: debug toggle, log viewer, S1 drill,
  S4 lab, live S5 DataLogging stats (records / median flush / verdict
  vs the 60 s gate), and a phone-local S6 drain estimate (watch-battery
  change points → %/h → projected days vs the 7-day gate). S7 is noted
  as covered by every latency drill (full PK2 round trip).

## Impact

- Specs: watch-phone-protocol (ADDED requirement)
- Code: detectors.{h,c} (lab hold + test, 118 → 122 checks), worker.c,
  main.c, protocol.h, Protocol.kt, MonitorService.kt, DebugActivity.kt
  (new), MainActivity.kt (diagnostics moved), manifest
