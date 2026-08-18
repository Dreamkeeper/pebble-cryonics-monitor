# Delta: product-requirements — foreground mode out of scope

## MODIFIED Requirements

### Requirement: Monitoring must not claim the watch
Monitoring SHALL run in the Pebble background worker so the wearer keeps
their chosen watchface; only events requiring immediate wearer action
may take the screen. The worker's single-slot eviction risk SHALL be
watchdogged from the phone. A persistent-foreground mode (watchapp
permanently on screen, watchface-fused) is REMOVED from scope by owner
decision (2026-08-18): S1 measured the worker alarm path at 71 ms
against a 3 s gate, and the watchface analysis showed no variant
retains the alert ladder's buttons — the hedge no longer buys anything.
The background-silence gap is addressed by DataLogging heartbeats
(M0 S5), not by claiming the screen.

#### Scenario: The watchface stays the wearer's
- **WHEN** monitoring is active and no alert needs attention
- **THEN** the wearer's watchface is displayed, not this app
