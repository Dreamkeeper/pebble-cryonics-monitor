# Design: platform-ui-guidelines

## Context

See proposal — guideline sources cited there. Constraint: keep the
programmatic-views approach (no Compose/XML-layout migration) while
getting genuine Material 3 rendering and correct density behavior.

## Decisions

**D1 — Material via theme + AppCompat inflater, not rewrites.** Adding
`Theme.Material3.DayNight.NoActionBar` and migrating activities to
`AppCompatActivity` makes programmatically-created `Button`/`Switch`/
`EditText` inflate as their Material counterparts through the
MaterialComponentsViewInflater — the entire widget set upgrades without
touching call sites. `DynamicColors.applyToActivitiesIfAvailable()` in
`CmApp` opts into Material You wallpaper color on Android 12+.

**D2 — dp grid via one helper.** `Ui.dp(context, n)` converts the M3
4dp grid; every `setPadding`/size call goes through it. This fixes the
real defect (raw px ≈ quarter-size on the owner's 3200×1440 phone).

**D3 — Type via M3 text appearances.** `TextView.setTextAppearance`
with `TextAppearance.Material3.{HeadlineSmall,TitleMedium,BodyMedium,
BodySmall}` replaces ad-hoc `textSize` values; color roles come from
the theme (`colorError` container for the DEGRADED banner instead of
hardcoded dark red).

**D4 — Adaptive icon, vector only.** Foreground: minimal snowflake/
pulse glyph vector; background: theme-independent deep teal. No PNG
pipeline.

**D5 — Pebble styling per the recommended guide.** Main window:
StatusBarLayer (time visible — guideline for long-running apps), state
line at 28pt equivalent (GOTHIC_28_BOLD), hints ≥18pt (GOTHIC_18);
background green only while genuinely monitoring, neutral when
suspended (semantic color rule), white-on-color text; monochrome
platforms keep black-on-white with bold hierarchy. Alert window: the
countdown number becomes the dominant element (LECO/BITHAM numeric
font), red background reserved for COUNTDOWN/ALARM stages on color
platforms (a genuine error state — allowed), amber for CHECKIN;
long-pulse vibe already used for attention per guideline.

## Risks / Trade-offs

- AppCompatActivity migration touches every activity's base class —
  mechanical, verified by compile + on-device pass.
- Dynamic color varies with wallpaper; the DEGRADED banner uses the
  error role specifically so it never blends in.

## Migration Plan

Pure client updates; sideload over existing installs.

## Open Questions

None.
