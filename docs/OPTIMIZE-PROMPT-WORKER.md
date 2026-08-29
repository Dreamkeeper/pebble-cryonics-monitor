# Optimization task: shrink the Pebble worker's RAM footprint

You are an embedded-systems optimizer with full access to this
repository. Unlike the earlier review task, you SHOULD change code —
but every change must be behavior-preserving and provable.

## The constraint that matters

The Pebble background worker's code, static data, stack, and heap all
share one ~10.5 KB RAM region. Binary size IS heap: the field trail
went 2112 → 1472 → 1216 → 960 → 192 B free as features were added,
and 192 B free means one system allocation (HR burst toggle, DL
flush) can crash the safety monitor at the worst possible moment.
A first pass (compiling out verbose log strings behind
`CM_WORKER_VERBOSE`) already cut `.text` 7532 → 6045 B. Your job is
to find the rest.

## Measurement (the only score that counts)

```bash
# WSL Ubuntu; SDK at ~/pebbleos-sdk-0.1.8
export PATH="$HOME/.local/bin:$PATH"
source ~/pebbleos-sdk-0.1.8/env.sh
cd <repo>/watchapp && pebble build
arm-none-eabi-size build/emery/pebble-worker.elf
arm-none-eabi-nm --size-sort -r build/emery/pebble-worker.elf | head -30
```

Baseline (2026-08-29): text=6045 data=2 bss=264. Report every change
with its measured delta. A change that saves nothing gets reverted.

## Where the bytes are (baseline symbol sizes)

- `cm_tick` 0x460 — the whole detector ladder (detectors.c, shared
  with the app and 163 host checks)
- `tick_handler` 0x3dc — worker per-second path (lab streaming,
  heartbeat, wall-jump detect, drill countdown)
- `worker_message_handler` 0x344, `main`/init 0x1f8,
  `drain_actions` 0x1d0, `cm_accel_feed` 0x140, `health_handler` 0x114
- `s_core` 0xe8 bss — the detector state struct

## Rules

1. **Host tests are the gate**: `watchapp/tests/test_detectors.c`
   (build line in its header; 163 checks) must pass after every core
   change. If you add risk, add a check.
2. **No behavior changes.** Same detector timings, same protocol
   bytes, same persisted formats. If a size win requires a behavior
   change, write it up as a PROPOSAL in the results file instead of
   implementing it.
3. **Keep WARNING/ERROR logs** and the `CM_WORKER_VERBOSE` structure.
4. The app (`src/c/main.c`) has a large RAM budget — moving
   worker-only complexity to the app side is allowed where the
   protocol permits; note the trade-off.
5. Compiler/toolchain flags: the SDK's wscript controls them; if you
   find a safe flag-level win (e.g., confirming `-Os`,
   `-ffunction-sections`/`--gc-sections` are active), document it —
   don't fork the SDK.

## Ideas worth checking (not instructions — verify, measure)

- Struct packing in `cm_core`/`cm_config` (field order, width);
  remember `cm_config` is persisted with a size check — a layout
  change invalidates stored config (acceptable: defaults reload, but
  say so).
- `switch` → table or vice versa in `drain_actions` /
  `worker_message_handler` — ARM Thumb codegen differences are
  real; measure both ways.
- Redundant `cm_current_stage()` calls / repeated `s_core.` loads
  the compiler cannot fold.
- The lab-streaming block in `tick_handler` (`WMSG_HR_SAMPLE`) — can
  it compile out for non-lab builds, or shrink?
- `isqrt32` vs a squared-threshold comparison in `cm_accel_feed`
  (compare mag² against threshold² — kills the sqrt entirely; needs
  care with the jerk delta which is linear).
- Duplicate string literals; float/64-bit ops sneaking in libgcc
  helpers (`nm` will show `__aeabi_*`).

## Output

Write `docs/WORKER-OPTIMIZATION-RESULTS.md`: baseline, each change
with measured before/after text/bss and the test-suite result, total
reclaimed, plus a PROPOSALS section for behavior-changing ideas you
did not implement. Leave the code changes uncommitted for review.
