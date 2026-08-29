# Pebble worker RAM optimization results

Date: 2026-08-29

## Result

The Emery worker footprint fell from 6311 to 6059 bytes without changing
detector timings, action semantics, protocol bytes, persisted formats, or
enabled features.

| Metric | Baseline | Final | Reclaimed |
|---|---:|---:|---:|
| `.text` | 6045 B | 5813 B | 232 B |
| `.data` | 2 B | 2 B | 0 B |
| `.bss` | 264 B | 244 B | 20 B |
| Total static footprint | 6311 B | 6059 B | **252 B** |
| SDK-reported free RAM before runtime allocations | 3929 B | 4181 B | **252 B** |

The SDK reports a 10,240-byte worker region. HealthService, DataLogging,
stack frames, and other system allocations still consume part of the
link-time free figure at runtime, so field low-water telemetry remains the
authoritative operational measurement. This work adds 252 bytes of margin
to that low-water value; it does not claim the link-time 4181-byte figure is
fully available during sensing.

## Measurement method

Every accepted candidate was rebuilt for Emery and measured with:

```bash
source ~/pebbleos-sdk-0.1.8/env.sh
cd watchapp
pebble build
arm-none-eabi-size build/emery/pebble-worker.elf
arm-none-eabi-nm --size-sort -r build/emery/pebble-worker.elf | head -30
```

The first and final measurements used a clean all-platform build. The SDK is
Pebble SDK 4.9.169 with GCC 14.2.1. A verbose build confirmed the existing
toolchain already enables `-Os`, `-ffunction-sections`, and
`-fdata-sections`, with linker `--gc-sections`; no SDK fork or flag change is
needed. The final ELF contains no `__aeabi_*` helper symbols.

## Accepted changes

Measurements are cumulative and show `text/bss` in bytes. A worker-only
change cannot affect detector-core behavior; the shared-core suite was run
after every accepted core edit and again on the final tree.

| # | Behavior-preserving change | Before | After | Delta | Detector gate |
|---:|---|---:|---:|---:|---|
| 1 | Pack normalized diagnostic booleans with shifts instead of six conditional expressions | 6045/264 | 6009/264 | -36 text | Worker-only; core unchanged |
| 2 | Cache the ladder stage while constructing a DataLogging heartbeat | 6009/264 | 6001/264 | -8 text | Worker-only; core unchanged |
| 3 | Reorder private `cm_core` runtime fields by alignment; `cm_config` and `cm_action` layouts remain unchanged | 6001/264 | 5973/244 | -28 text, -20 bss | 163/163 passed |
| 4 | Inline the one-load `cm_current_stage` accessor at shell call sites | 5973/244 | 5957/244 | -16 text | 163/163 passed |
| 5 | Inline `cm_suspend_remaining_s` at its shell call site | 5957/244 | 5949/244 | -8 text | 163/163 passed |
| 6 | Inline `cm_stage_remaining_s` at its shell call site | 5949/244 | 5937/244 | -12 text | 163/163 passed |
| 7 | Inline `cm_suspend_sync_remaining` at its shell call site | 5937/244 | 5933/244 | -4 text | 163/163 passed |
| 8 | Outline the repeated age calculation rather than inlining it into status and heartbeat paths | 5933/244 | 5857/244 | -76 text | Worker-only; core unchanged |
| 9 | Outline the identical four-field detector-baseline reset used at five transitions | 5857/244 | 5845/244 | -12 text | 163/163 passed |
| 10 | Keep the large status-message encoder out of `worker_message_handler` | 5845/244 | 5837/244 | -8 text | Worker-only; core unchanged |
| 11 | Share scheduled-check-in deadline reset code across three paths | 5837/244 | 5825/244 | -12 text | 163/163 passed |
| 12 | Share pulse-snooze timestamp reset code across two cancellation paths | 5825/244 | 5821/244 | -4 text | 163/163 passed |
| 13 | Share the identical charging/lab hold-entry logic | 5821/244 | 5813/244 | -8 text | 163/163 passed |

`cm_core` is runtime-only and is neither persisted nor transmitted. Its
field reorder therefore changes no durable format. `cm_config` remains the
same size/layout for `PK_CONFIG`, and `cm_action` remains the same size/layout
for `PK_PENDING_ACTION`.

## Candidates measured and reverted

The following experiments were removed because the total static footprint
did not improve:

| Candidate | Measured effect | Reason reverted |
|---|---:|---|
| Zero `cm_core` with word stores | 0 B total | No score improvement |
| Inline `cm_next_action` | +168 B text | Large queue-copy expansion in `drain_actions` |
| Replace `drain_actions` jump table with grouped conditionals | +8 B text | Jump table is smaller |
| Outline `set_hr_burst` | +8 B text | Existing inlining is smaller |
| Outline `log_heartbeat` | +16 B text | Existing inlining is smaller |
| Pack thirteen `cm_core` booleans as C bitfields | +180 B text, -8 B bss; +172 B total | Masking cost greatly exceeds RAM saved |
| Add a second shared wear-evidence reset helper | 0 B total | No score improvement |
| Outline `countdown_len` | 0 B total | No score improvement |
| Share two persisted-suspension delete sequences | 0 B total | No score improvement |
| Replace four episode action comparisons with an enum range | 0 B total | Compiler already emits equivalent code |

## Verification

- GCC host detector suite: **163 checks, 0 failures**.
- MSVC host detector suite (`/W4 /std:c11`): **163 checks, 0 failures**.
- Clean Pebble build: successful for Gabbro, Flint, Emery, and Diorite.
- Final Emery worker: `text=5813`, `data=2`, `bss=244`, total `6059`.
- Final PBW produced at `watchapp/build/watchapp.pbw`.

The build retains one pre-existing non-verbose warning for
`motion_before`, whose use is compiled out with `CM_WORKER_VERBOSE=0`, plus
the SDK linker's existing RWX-segment warnings. Neither affects the measured
footprint. WARNING/ERROR logging and the `CM_WORKER_VERBOSE` structure remain
intact.

## Proposals requiring behavior or format changes

These ideas were deliberately not implemented.

1. **Separate production and sensor-lab worker profiles.** Compile the S4
   raw-HR/quality streaming block and its state out of a production PBW, and
   ship a separately identified diagnostic PBW for feasibility work. This
   should remove a substantial part of `tick_handler`, but the production
   build would no longer support Sensor Lab, so it is a product/build-profile
   decision rather than a behavior-preserving optimization.

2. **Redesign motion detection around squared/vector thresholds.** Removing
   `isqrt32` is only safe after defining and calibrating a new jerk metric.
   Comparing magnitude squared can preserve freefall/impact ordering, but
   the current jerk threshold is linear (`abs(mag[n]-mag[n-1])`) and has no
   exactly equivalent squared comparison. This requires new thresholds,
   field calibration, and detector-spec changes.

3. **Pack or version `cm_config`.** The config has a one-byte theoretical
   packing opportunity, but changing it invalidates persisted `PK_CONFIG`
   blobs and can introduce unaligned accesses. A future versioned config
   format could instead store compact wire fields and expand into runtime
   state on boot.

4. **Reduce or externalize the eight-entry action queue.** This could reclaim
   multiples of the eight-byte `cm_action`, but it changes overflow tolerance
   and therefore missed-alarm behavior. It must not be done without queue
   saturation traces, an explicit delivery invariant, and new tests.

5. **Move optional diagnostics to the foreground app or firmware logging.**
   The worker could emit a smaller fixed liveness record while the app or a
   diagnostic firmware build gathers detailed HR/quality telemetry. This
   changes diagnostic availability when the app is closed and needs an
   explicit M0/testability decision.
