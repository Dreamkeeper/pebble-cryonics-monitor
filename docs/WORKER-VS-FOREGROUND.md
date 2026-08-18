# Background worker vs. watchface-fused foreground app

What each architecture can and cannot do on Pebble, and what that means
for a safety monitor. Sources: the official background-worker guide and
design docs on developer.repebble.com, plus this project's own build
probes and field runs (M0 spikes, docs/M0-SPIKES.md). Last reviewed
2026-08-18 against SDK 4.9.169.

## The three process types

Pebble OS distinguishes three things people casually call "the app":

1. **Watchface** — the default screen. Runs only while displayed. Gets
   **no buttons**: Up/Down belong to the timeline, Select and Back to
   the system. No menus, no click handlers. Full AppMessage access.
2. **Watchapp** — launched from the system menu (or by a worker/phone).
   Full UI, all buttons, vibration, AppMessage, resources. Only one
   watchapp/watchface is alive at a time — launching anything else
   kills it.
3. **Background worker** — a separate tiny binary bundled with a
   watchapp. Runs regardless of what is on screen; survives the wearer
   opening other apps and watchfaces.

Our "worker mode" = worker (detectors) + watchapp (alert UI), launched
on demand. The proposed "foreground mode" = our watchapp permanently on
screen, drawing a watchface-style display — it must be a watch**app**,
not a watchface, because check-ins/suspension need buttons.

## Capability matrix

| Capability | Background worker | Foreground watchapp (watchface-fused) |
|---|---|---|
| Accelerometer, HealthService (HR), Battery, Connection, Tick, Storage | ✅ | ✅ |
| DataLogging to phone | ✅ | ✅ |
| **AppMessage to phone** | ❌ (docs: workers "cannot use AppMessage") | ✅ full |
| **UI / windows** | ❌ no UI APIs | ✅ |
| **Vibration** | ❌ — verified by build probe 2026-08-18: `vibes_short_pulse()` is not even declared in the worker API | ✅ |
| Resources (fonts, images) | ❌ cannot load | ✅ |
| Buttons | n/a (no UI) | ✅ all four (a true watchface would get none) |
| Memory | **10.5 kB** heap (detector core + HR subscription ≈ fits; see S8 telemetry) | 256 kB on emery (Time 2) |
| Runs while wearer uses other apps/watchfaces | ✅ — that is its purpose | ❌ — any launch of anything else kills it |
| System-wide slots | **One worker total, across all apps.** Installing another worker app evicts ours (user is prompted). | Unlimited installs; only one on screen |
| Survives reboot | Undocumented — M0 spike S2 | Definitely not (user is on a watchface after boot) |
| Alarm path | Must `worker_launch_app()` and hand off via persist (measured by the S1 latency drill) | Direct: it already owns the screen |
| Wearer can accidentally disable it | Hard (Settings → Background App is buried) | **Easy: one Back press exits.** Needs a click-config Back override + a "hold Back to really quit" pattern |

## What this means for a safety monitor

**Worker mode (current default) wins on unattended reliability.** The
wearer keeps their favorite watchface, uses other apps, and monitoring
never stops. Its three real weaknesses:

1. **Silent while silent.** No AppMessage means the phone cannot hear
   from the watch between app launches — today's "synced Xm ago" ages
   until an alert or manual open. DataLogging is the documented worker
   escape hatch for exactly this; whether it flushes fast enough is M0
   spike S5 (watch half already implemented).
2. **Eviction.** One worker system-wide: installing any other
   worker-bearing app (sleep trackers are the classic case) evicts the
   monitor after a user prompt. The companion's link watchdog plus
   `startWatchapp()` self-heal covers detection; the wearer must not
   say yes to the eviction prompt.
3. **Launch latency.** Every wearer-visible event pays the
   `worker_launch_app()` cold start (S1 drill measures it; gate < 3 s).

**Foreground mode wins on immediacy and visibility, loses on fragility.**
Zero launch latency, continuous AppMessage heartbeats (the phone-side
"synced" age stays seconds-fresh), and the status is always on the
wrist. But: one Back press (or launching the weather app) and
protection is gone until the wearer notices. The mitigation set —
Back-button override, re-entry nagging from the companion, treating
"app not running" as a fault — is exactly the complexity the worker
architecture avoids. It also costs battery: full-color rendering,
second ticks, and a live AppMessage session outdraw a 10.5 kB worker.

**The fusion constraint.** Merging with YaForecasWatch2 means porting
its *rendering* into our watchapp — not shipping it as a watchface.
Watchfaces get no buttons, and its PebbleKit JS weather fetch cannot
coexist with our PebbleKit Android companion, so weather data would
have to come from the companion over the same AppMessage channel.
License is compatible (GPL-3.0 both ways).

## Recommendation (unchanged from the plan, now evidence-backed)

Keep **worker mode as the default**; ship **foreground mode as the
opt-in "high-assurance" profile** for wearers who accept tending the
screen (sleep hours, high-risk periods). Close the worker's silence gap
via S5 (DataLogging heartbeats) rather than by switching architectures:
if S5's median flush is < 60 s, worker mode loses its only structural
disadvantage that matters to the mission. Both modes ship in one binary
(PK_MODE persist key already reserved), so the wearer can switch per
situation.

## Open questions feeding this report

- S2: does the worker auto-restart after reboot / battery death?
- S5: DataLogging flush latency + PebbleKit2 receiver support.
- S6: battery cost of the worker profile (trail recording since
  2026-08-18); a foreground-mode battery run only matters if S6 passes.
