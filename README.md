# Set Time & Weather FH2

An [Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions) plugin for Forza
Horizon 2 that lets you **lock the free-roam time of day** and **choose the free-roam weather**,
right from the game's own **Settings > Video** menu. Both are dynamic (the normal game) by
default, and your choices are remembered between sessions.

Everything happens in memory while the game runs. No game file is replaced or modified, every
patch checks the exact game bytes it expects first, and anything unexpected leaves the game stock
and writes a line to the log. Time and weather are set through the game's own functions (the same
ones its event flow uses), never by writing values into the world directly.

<!-- Screenshot placeholders: add images under docs/screenshots/ and uncomment.
![The new rows in Settings > Video](docs/screenshots/settings-video.png)
![Free roam locked at sunset](docs/screenshots/sunset.png)
![Free roam with Storm weather](docs/screenshots/storm.png)
-->

> **Screenshots:** _coming soon_ (see the placeholders in this file's source).

---

## Requirements

You need **all** of the following. These are the exact versions Set Time & Weather was built and
tested with; other versions are not supported, and on a different game build the plugin's byte
checks fail and it simply does nothing (see [Troubleshooting](#troubleshooting)).

| Requirement | Version tested | Where to get it | Why it's needed |
|---|---|---|---|
| **Forza Horizon 2** (Xbox One version) | Build **1.0.0.64** (`Anthem_1.0.0.64_x64`) | Your own copy. **You must own the game**; nothing from it is included here. | The game itself. Every address the plugin patches is specific to this build's `forza_durango_release_final.exe`. |
| Forza Horizon 2 **Storm Island** DLC | 1.0.0.36 | Your own copy | **Optional.** The mod works with or without it. |
| **Azimuth** | **1.0.1** | [almost-there-games.itch.io/azimuth](https://almost-there-games.itch.io/azimuth) | The translation layer that runs the Xbox One game on Windows. Extra Options (and so this plugin) runs inside it. |
| **Forza Horizon 2 Extra Options** by RokkuDayo | **0.3.0** | [github.com/RokkuDayo/ForzaHorizon2_ExtraOptions](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions) | The mod loader and plugin API that loads this plugin from `mods\`, and the UI hook the Settings > Video rows chain onto. |

Tested on Windows 11 (64-bit).

### Extra Options settings this mod depends on

These live in `FH2_ExtraOptions.ini`, beside `Azimuth.exe`. The defaults that ship with Extra
Options 0.3.0 are fine.

| Setting | Needed value | What it's for | If it's off |
|---|---|---|---|
| `[Global] Enabled` | `1` | Turns Extra Options' features on, including its UI hook. | The time and weather hold still work from `TimeLock.ini`, but the Settings > Video rows don't appear. Log: `TIME LOCK PAGE: Extra Options' UI hook isn't active; no Settings rows (TimeLock.ini still works).` |
| Mod loading (`mods\loadorder.txt`) | `Set Time & Weather FH2` listed | Extra Options only loads mods listed there. | Nothing from this mod loads: no `TIME LOCK` lines in the log at all. |
| Extra Options' UI hook | active (automatic with Extra Options 0.3.0 enabled) | The three Settings > Video rows are inserted into that page as the game loads it, chained after Extra Options' own edits to the same page. | Same as above: no rows, `TimeLock.ini` still works. |

`[Frontend] GetInCarFromPauseMenu` and `[Files] EnableMediaOverrides` are **not** needed: this
mod ships no media files and doesn't use Get in Car.

### Compatibility notes

- **Enhanced Tuning** (the companion mod) works alongside Set Time & Weather; both chain onto
  Extra Options' UI hook, in either load order.
- **Extra Options' own Video page rows** (FOV sliders, shadows) are kept: the new rows go after
  everything already in the list, and the list's height is only changed if it is still the stock
  height.
- **FH2 Mod Platform / FnFNitro and the KITT (Knight Rider) mod** both hook the speedometer at
  `game+0xAD6070`, so don't run *their* HUD hooks together with each other. Set Time & Weather
  does not touch that address or any HUD page, so it isn't affected either way.
- Intended for offline / single-player free roam. Online sessions are untested.

---

## Features

Three new rows in **Settings > Video**, after Brightness and Subtitles (and after any rows other
mods added):

| Row | Values | What it does |
|---|---|---|
| **Time of Day Lock** | Off / On | On: holds the free-roam clock at the Locked Hour. Off (default): the normal day/night cycle. |
| **Locked Hour** | 0 – 23.75, in quarter hours | The hour to hold (6 = dawn, 12 = noon, 17.75 = sunset, 23 = night; 18.5 = 18:30). Applies immediately while the lock is on. |
| **Weather** | Current / Sunny / Wet / Rain / Storm | Holds free-roam weather at one of the game's own weather options (the same list as the online event options). **Current** (default) keeps the game's normal weather cycle. |

- Changes apply right away and are saved to `TimeLock.ini`.
- **Races, car meets and events** that set their own time or weather keep it; free roam returns
  to your choice afterwards.
- A clock the game has stopped or locked itself (fixed-time events, cutscenes) is never touched.
- Weather options: **Sunny** is clear and dry, **Wet** is overcast with damp roads and no rain,
  **Rain** is rain with wet roads, **Storm** is heavy rain with a storm sky.

## Install

1. Install and set up **Azimuth 1.0.1** and **Extra Options 0.3.0** first, and check the game runs
   with Extra Options (there is an `FH2_ExtraOptions.log` beside `Azimuth.exe`).
2. Download **`FH2-SetTimeWeather-1.2.0.zip`** from this repository's **Releases** page and drag the **`mods`** folder inside it onto your Azimuth folder (the one with `azimuth.exe`). Say yes if Windows asks to merge folders. You get:

   ```
   <Azimuth folder>\mods\Set Time & Weather FH2\mod.json
   <Azimuth folder>\mods\Set Time & Weather FH2\TimeLock.ini
   <Azimuth folder>\mods\Set Time & Weather FH2\plugins\TimeLock.dll
   ```

3. That's it: Extra Options turns on any new mod folder it finds in `mods\` the next time the game starts, so you don't need to edit `loadorder.txt`. (It adds the mod at the end of your load order; move the `Set Time & Weather FH2` line if you want it elsewhere, and change it to `# Set Time & Weather FH2` to switch the mod off.)
4. Start the game. `FH2_ExtraOptions.log` should contain
   `TIME LOCK: ready (clock hold on, weather hold on, Settings rows on).`

## Usage

1. Open **Settings > Video** (from the pause menu or the main menu).
2. Scroll past Brightness and Subtitles to **Time of Day Lock**, **Locked Hour** and **Weather**.
3. Turn the lock on and pick an hour, and/or pick a weather. Back out to free roam: the clock and
   weather follow your settings.
4. To return to the normal game, set Time of Day Lock to **Off** and Weather to **Current**.

## Settings (`TimeLock.ini`)

In the mod folder. The in-game rows write `Enabled`, `Hour` and `Mode` here; you can also edit the
file while the game is closed (it is read at game start).

```ini
[TimeLock]
; 1 = hold the free-roam clock at Hour, 0 = normal day/night cycle (default)
Enabled=0
; 0-23.99 hours (18.5 = 18:30), HH:MM (18:30), or a preset name: Dawn, Noon, Sunset, Night
Hour=Noon

[Weather]
; Dynamic (default; shown as "Current" in game), Sunny, Wet, Rain or Storm
Mode=Dynamic

[Presets]
; Hours for the Hour= preset names; change them freely
Dawn=5:45
Noon=12:00
Sunset=17:50
Night=23:00

[Settings]
; 1 = add the three rows to Settings > Video (needs Extra Options' UI hook)
ShowInGameSettings=1
; Troubleshooting only: 1 = save a copy of the Video settings page as the game loads it
; (seen_A_VideoOptions.xaml) to check why the rows didn't appear. Default 0.
; The copy is your own game's file: don't share or redistribute it.
SaveSettingsPageCopy=0
```

## Uninstall

1. Remove the `Set Time & Weather FH2` line from `mods\loadorder.txt`.
2. Delete `mods\Set Time & Weather FH2\`.

No game file was ever changed, and the next start is stock.

---

## Troubleshooting

Everything is logged to **`FH2_ExtraOptions.log`** (beside `Azimuth.exe`) on lines starting with
`TIME LOCK`. A healthy start looks like this:

```
TIME LOCK: settings loaded: Enabled=0, Hour=Noon (12:00).
TIME LOCK: weather setting loaded: Mode=Dynamic.
TIME LOCK: hooked TimeOfDay::Update at game+0x197b170.
TIME LOCK: weather hold ready (WeatherManager::SetControl at game+0x1981860).
TIME LOCK PAGE: TimeLockEnabled/TimeLockHour/TimeLockWeather registered on the VideoOptions type.
TIME LOCK PAGE: chained onto Extra Options' UI hook.
TIME LOCK: ready (clock hold on, weather hold on, Settings rows on).
```

and, when you open Settings > Video for the first time: `TIME LOCK PAGE: Time of Day Lock rows added to Settings > Video.`

| Symptom | Log line to look for | What to do |
|---|---|---|
| No `TIME LOCK` lines at all | — | The mod isn't loaded. Check the folder is `mods\Set Time & Weather FH2\` with `plugins\TimeLock.dll` inside, and that the folder name is in `mods\loadorder.txt`. |
| Nothing works | `TIME LOCK: unexpected game code at game+0x... (...); nothing patched.` / `TIME LOCK: could not hook TimeOfDay::Update (already hooked or busy); nothing patched.` | The game build isn't 1.0.0.64, or another mod hooks the same game function. |
| Weather doesn't change | `TIME LOCK: unexpected game code ... weather not held.` / `TIME LOCK: weather call chain didn't match; weather not held.` | Same: game build or another mod. The time lock still works. |
| No rows in Settings > Video | `TIME LOCK PAGE: Extra Options' UI hook isn't active; no Settings rows (TimeLock.ini still works).` | Extra Options isn't enabled or isn't 0.3.0. Use `TimeLock.ini` meanwhile. |
| | `TIME LOCK PAGE: Video settings list did not match; rows not added.` / `... resources did not match ...` | Another mod changed the Video page's layout. Set `SaveSettingsPageCopy=1`, restart, and look at `seen_A_VideoOptions.xaml` in the mod folder (don't share it: it's the game's file). |
| The clock isn't held in an event | `TIME LOCK: the game has locked this clock itself (fixed-time or stop lock); leaving it alone.` | Expected: events that set their own time keep it. |
| Hour or weather in the ini ignored | `TIME LOCK: Hour=... in TimeLock.ini is not 0-23.99, HH:MM or a preset; using 12:00.` / `TIME LOCK: [Weather] Mode=... is not Dynamic, Sunny, Wet, Rain or Storm; using Dynamic.` | Fix the value in `TimeLock.ini`. |

When reporting a problem, include the `TIME LOCK` lines from the log and your versions of the
game, Azimuth and Extra Options.

---

## Build from source

Requirements:

- **Zig 0.15.2** ([ziglang.org/download](https://ziglang.org/download/)), the same compiler Extra
  Options uses. Only `zig cc` is used; the plugin is plain C.
- Windows (the build script is a `.cmd` file).
- **Python 3** (standard library only) for the offline tests.
- **Your own Forza Horizon 2 1.0.0.64 install** for the offline tests: its `Mount` folder, the one
  that holds `forza_durango_release_final.exe` and `media\UI.zip`.

Build:

```
build.cmd C:\path\to\zig.exe
```

(or just `build.cmd` if `zig` is on your `PATH`). This writes
`release\Set Time & Weather FH2\plugins\TimeLock.dll` and the test harness
`tests\offline_test.exe`. The build uses `-Wall -Wextra -Werror`.

### Offline tests

```
python tests\run_tests.py "C:\path\to\Forza Horizon 2\Anthem_1.0.0.64_x64__8wekyb3d8bbwe\Mount"
```

(or set `FH2_MOUNT` to that folder and run `python tests\run_tests.py`). The tests read your game
files **read only** and never change them:

- every byte guard is checked against your real game EXE, and a flipped byte must make it refuse;
- the real clock hook is installed into a mapped copy of the EXE and drives the game's own
  time-of-day code through the free-roam, race, hour-change and lock-off cases;
- the weather hold's logic runs against a fake weather manager;
- the Settings > Video edit runs on the page extracted from your `media\UI.zip`, on a copy with
  Extra Options-style edits and on a copy edited by another mod, and the results are checked
  for well-formed XML, row order, list height and resources; other option pages must stay
  untouched.

The extracted and edited pages are written to `tests\work\`, which is git-ignored. **Never commit
it**: those are the game's files. No game files are included in this repository.

### Repository layout

| Path | What |
|---|---|
| `src\` | Plugin source (C). `timelock.c` is the entry point; `clock_hold.c` (time), `weather_hold.c` (weather), `settings_page.c` (Settings rows), `config.c` (TimeLock.ini). `fh2_plugin.h` is Extra Options' plugin API header. |
| `tests\` | Offline test harness (`offline_test.c`) and runner (`run_tests.py`). |
| `release\Set Time & Weather FH2\` | The ready-to-copy mod folder, as built. |
| `docs\time_of_day.md` | Research notes: how free roam drives time and weather, and how the mod holds them, with the game addresses (RVAs) involved. |
| `build.cmd` | Build script. |

---

## Credits and legal

See [CREDITS.md](CREDITS.md). This project is not affiliated with or endorsed by Playground Games,
Turn 10 Studios, Microsoft, Almost There Games or the Extra Options author. **No game files are
included.** You need your own copy of Forza Horizon 2.

License: [GPL-3.0](LICENSE), the same licence as Extra Options, whose plugin API header `src\fh2_plugin.h` is included.

---

## AI use

This project was made with the help of AI (Claude by Anthropic). It was tested in game, but treat it as hobby software and keep backups of your saves.
