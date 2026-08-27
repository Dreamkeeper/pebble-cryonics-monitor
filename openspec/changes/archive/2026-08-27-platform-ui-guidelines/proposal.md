# Proposal: platform-ui-guidelines

## Why

Both apps work but look like engineering scaffolding: the Android app
uses raw-pixel padding (cramped on high-density screens), framework
widgets with no theme, no dark-mode discipline, no app icon; the
watchapp uses small default fonts and no color semantics. The owner
asked for the actual platform guidelines to be looked up and applied.
Sources consulted: Android Material Design 3 guidance
(developer.android.com/design → m3.material.io: M3 theme, dynamic
color, type scale, 4dp grid, 16/24dp paddings, dark theme) and the
official Pebble guidelines (developer.repebble.com/guides/design-and-
interaction/recommended/: glanceable type — 28pt for key data, 18pt
minimum; semantic color only where meaning exists; StatusBarLayer for
long-running apps; full-screen modals for significant events; long
vibration pulse for failures needing attention).

## What Changes

- Android: Material 3 DayNight theme with dynamic color
  (com.google.android.material dependency, `DynamicColors` opt-in in
  the Application class), activities migrate to AppCompatActivity so
  framework widgets inflate as Material components, all raw-px spacing
  replaced with a dp-grid helper (16/24dp rhythm), M3 type scale
  applied to programmatic text, DEGRADED banner restyled with the M3
  error color role, adaptive launcher icon.
- Watchapp: StatusBarLayer on the main window (long-running app with
  time reference per guidelines), key data at 28pt equivalents /
  nothing under 18pt, semantic color on color platforms (green =
  monitoring OK, red only on genuine alarm states, amber pre-alarm),
  monochrome-safe fallbacks for diorite/flint, alert countdown as the
  dominant element.
- No behavior, protocol, API, or spec-level change: `skip_specs`.

Alarm-path impact: none — presentation only; alert stages, timings,
and message flows are untouched.

## Non-goals

- Jetpack Compose migration; XML layout migration (views stay
  programmatic, restyled).
- Round (gabbro) bespoke layout beyond safe centering (tracked in the
  product ledger already).
- Dashboard visual redesign (already M3-ish minimal; not in scope).

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
(none — visual/quality change, skip_specs)

## Impact

- `android/`: theme resources, icon resources, CmApp, all activities,
  Ui helper gains dp()/typography utilities; material dependency.
- `watchapp/src/c/main.c`: window styling only.
- Builds: new APK + .pbw; no server change.
