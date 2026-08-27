# Tasks: platform-ui-guidelines

## 1. Android Material 3

- [x] 1.1 material dependency; Theme.Material3.DayNight theme resources;
      manifest theme; DynamicColors in CmApp
- [x] 1.2 Activities → AppCompatActivity (Main, Contacts, Enroll, Log,
      Alarm); verify Material widget inflation
- [x] 1.3 Ui.dp() + M3 typography helpers; replace all raw-px spacing
      and ad-hoc text sizes across screens
- [x] 1.4 DEGRADED banner + alarm screen on M3 color roles (error
      container); adaptive launcher icon (vector)
- [x] 1.5 Unit tests still green; build sideload APK

## 2. Pebble guideline styling

- [x] 2.1 Main window: StatusBarLayer, GOTHIC_28_BOLD state line,
      ≥18pt hints, semantic background colors w/ monochrome fallback
- [x] 2.2 Alert window: dominant countdown numerals, stage-semantic
      colors (amber check-in / red countdown+alarm on color platforms)
- [x] 2.3 Detector suite green; pebble clean build; emulator or
      on-watch install check

## 3. Wrap-up

- [x] 3.1 dist refresh (.pbw + .apk), commit
- [x] 3.2 Owner verdict (2026-08-27): M3 baseline accepted as interim
      ("far from the final form, but should not be the main focus for
      now") after two weeks of daily field use. Deeper UI work is
      deferred past M0/M1 and will arrive as its own change.
