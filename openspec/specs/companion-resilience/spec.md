# companion-resilience Specification

## Purpose
TBD - created by archiving change soak-and-recovery. Update Purpose after archive.

## Requirements

### Requirement: Monitoring survives phone reboots
When the companion is configured (server URL set), the phone SHALL
resume monitoring after a reboot without wearer action: a boot
receiver starts the foreground service on `BOOT_COMPLETED` and after
app updates on `MY_PACKAGE_REPLACED`. The service start path from the
receiver MUST tolerate Android 14/15 foreground-start restrictions
(the existing typed-fallback chain applies). Because HyperOS/MIUI
suppresses boot broadcasts without the vendor "Autostart" permission,
the app SHALL surface that requirement to the user (documentation +
recovery-lab verification); it MUST NOT silently assume boot recovery
works on such devices.

#### Scenario: Reboot while configured
- **WHEN** the phone reboots and OEM autostart is permitted
- **THEN** MonitorService is running (foreground notification present)
  within the boot-recovery gate (2 min of boot) with no user action,
  and the soak counters record a boot-recovery start

#### Scenario: Reboot while unconfigured
- **WHEN** the phone reboots before enrollment (no server URL)
- **THEN** the receiver starts nothing (no crash loops, no zombie
  notification for an unconfigured app)

### Requirement: Soak counters
MonitorService SHALL persist monotonic counters across restarts:
service starts (split: boot-recovery / other), watch disconnect
events and cumulative link-down seconds, DataLogging records
received, worker-silent faults, self-heal launches, pre-alarms and
alarms raised, server send failures. Counters survive process death,
are cheap (SharedPreferences, no timers), and are resettable only
from the Debug screen.

#### Scenario: A week of wear produces a verdict
- **WHEN** the wearer opens Debug after N days of normal wear
- **THEN** the Soak card shows the since-reset window and every
  counter, and Share exports them as text for the soak log

### Requirement: Guided recovery lab
The Debug screen SHALL provide a confirmation-gated recovery lab
(sensor-lab interaction pattern: instruct → confirm → measure →
verdict, abort at any stage) covering at minimum: (a) phone-reboot
recovery — arm, reboot, and on next Debug open the lab reports
PASS/FAIL against the boot-recovery gate using persisted markers;
(b) watch-outage recovery — watch powered off ≥5 min, lab reports
disconnect-detection latency and reconnect latency from the link
events. Results persist until the next lab run and are shareable.

#### Scenario: Reboot drill without a stopwatch
- **WHEN** the user arms the reboot stage and reboots the phone
- **THEN** on the next Debug open the lab shows PASS with the
  measured boot→service delay, or FAIL with "service did not start —
  check HyperOS Autostart", with no manual timing
