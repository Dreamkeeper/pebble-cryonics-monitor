# Proposal: drop-foreground-mode

## Why

The persistent-foreground mode (watchapp permanently on screen, fused
with the YaForecasWatch2 watchface) existed as a hedge against two
worker-mode risks: alarm-path latency and background silence. The
evidence now says the hedge buys nothing:

- M0 spike S1 measured the worker→app cold alarm path at **71 ms**
  (gate was 3 s) — the latency risk is gone.
- S3 confirmed the launch-app handoff is the only alarm path either
  way (workers cannot vibrate), so foreground mode never had a
  structural advantage there.
- The watchface+worker variant was analyzed separately
  (docs/WORKER-VS-FOREGROUND.md) and strictly loses (no buttons).
- The silence gap has a better fix (S5 DataLogging heartbeats) that
  keeps the wearer's own watchface.

Owner decision 2026-08-18: remove foreground mode from scope entirely.

## What changes

- product-requirements: the "planned opt-in alternative" language is
  removed; worker mode is THE architecture. The undelivered-scope list
  drops the YaForecasWatch2 merge.
- No code change: foreground mode was never implemented (PK_MODE
  persist key stays reserved but documented as unused).
- docs/WORKER-VS-FOREGROUND.md records the decision and evidence.

## Impact

- Specs: product-requirements (MODIFIED requirement)
- Code: none (comment-level notes only)
