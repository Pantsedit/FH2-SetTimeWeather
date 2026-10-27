# Changelog

## 1.2.0 (first public release)

Changes from the development build for release:

- `[Settings] SaveSettingsPageCopy` now defaults to **0** (code and `TimeLock.ini`). The
  development build saved a copy of the game's Video settings page (`seen_A_VideoOptions.xaml`)
  to the mod folder on every start; that is now opt-in, for troubleshooting only.
- `build.cmd` writes the plugin to `release\Set Time & Weather FH2\plugins\` (was `dist\`).
- `tests/run_tests.py` no longer has a built-in path to a local game install: pass your FH2
  `Mount` folder as the first argument or set `FH2_MOUNT`. It exits with a message if neither
  is given.
- The extracted test pages (`tests/work/`) and the built test harness are not part of the
  repository (git-ignored).
- README, CREDITS and research notes (`docs/time_of_day.md`, paths updated) added.

No gameplay feature changed.
