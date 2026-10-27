# Time of day and weather in FH2: how free roam drives them, and how Time Lock holds them (2026-10-27)

This is static research on `forza_durango_release_final.exe` (Xbox One build 1.0.0.64, run under Azimuth).
All addresses are RVAs from the image base (game+X), the same convention as the LiveTune reports.
The disassembly came from capstone (the Enhanced Tuning repository's `tools/disasm.py` and similar small
helper scripts, plus the RTTI vtable list). Nothing here was run in the game. The offline tests drive the game's own
TimeOfDay code from a mapped copy of the EXE (see the end of this report).

Confidence: **high** means read directly in code, **medium** means inferred from code plus names or data flow,
and **low** means a guess.

## TL;DR

* Each loaded world owns one time-of-day object, `T = world->[0x480]`. It is created at track load in game+0x2e4190
  (`0x2e5995: mov [r15+0x480], rsi`). Game code reaches it through
  `0x2e1f90(0x21e0c0([0x34aba60], 0))`, which returns `[[[0x34aba60]+0x48]+0x98]+0x480`. (high)
* The world update runs `TimeOfDay::Update(T, dt, out)` = **game+0x197b170** once per frame, and has a
  single caller at game+0x300933. Update does `if (!T.stopped) T.time += dt * T.speed`, wraps at 86400,
  and ends a timed transition at its target. (high)
* The stateflow can only change the clock through 5 behaviours. They all call a few tiny TimeOfDay
  methods. Free roam's `finalise_free_roam_common` ends with `global.start_time`, which is
  `StopTimeOfDay stopped="false"`. So in free roam, **the clock is running with no target**. (high)
* **Time Lock** hooks Update's entry. When the lock is on and the clock is running freely, it does
  what the game's own `set_time_of_day` + `stop_time` states do: it calls the game's
  **SetTime(T, hour*3600, 1)** (game+0x197aec0) and sets `T.stopped = 1`. It never touches a clock
  that the game stopped, gave a target to, or locked itself. (implemented, tested offline)
* No Settings screen has a hidden free-roam time-of-day option. `EnumDataSource<TimeOfDayOptions>` and
  `IDS_TimeOfDay_Sunrise/Midday/Sunset/Midnight/Current` belong to the **online event / race options**
  (the `UI/OnlineEventOptions/TimeOfDay/*` strings, read by `SetTimeOfDayFromGameOptions` at race
  start). Time Lock adds its own rows to **Settings > Video** instead, after Brightness and Subtitles. (medium-high)

* **Weather** is held in the same way (section 6), through the game's own WeatherManager::SetControl (game+0x1981860).
  It applies the game's own Sunny/Wet/Rain/Storm option values, and only while the game has released free-roam
  weather to dynamic. It runs from the same clock hook, so there is no new hook.

## 1. The TimeOfDay object

Vtable `TimeOfDayComponent` 0x2f646e8, factory `TimeOfDayFactory` 0x2f64788, and loader 0x197aae0, which
reads `game:\media\TimeOfDay\XboxOne\*.fxb`. Fields used by the clock (high, each one is read or written in
the functions named):

| offset | type | meaning | evidence |
|---|---|---|---|
| +0x0c | float | previous frame's time | Update 0x197b1a6, 0x197b21a |
| +0x10 | float | **time**: seconds since midnight, internal (after the remap curve) | SetTime, Update |
| +0x14 | float | interval for the sky keyframe lookup | Update 0x197b2ba |
| +0x18 | float | **speed** (game seconds per real second; the stateflow default is 48) | Update 0x197b1bf, SetTargetWithSpeed 0x197b039 |
| +0x1c | float | target time | SetTarget 0x197aff8, Update 0x197b1ee |
| +0x20 | float | time when the target was set | SetTarget |
| +0x24 | byte | **stopped** | Update 0x197b1ac, StopTimeOfDay 0x93ca26 |
| +0x25 | byte | fixed time: set by a debug override at load, and **SetTime ignores calls** while it is set | loader 0x197ad6c, SetTime 0x197aec6 |
| +0x26 | byte | a speed override is active | SetTargetWithSpeed, ClearTarget |
| +0x27 | byte | stop lock: **StopTimeOfDay ignores calls** while it is set (the same check is at 0x4087e6) | 0x93ca20 |
| +0x28 | byte | **has target**: a timed transition is running | SetTarget, Update, ClearTarget 0x197a4e0 |
| +0x29 | byte | sky/lighting evaluation enabled (Update returns early when it is 0) | Update 0x197b211 |
| +0x2a | byte | "big jump" flag (time moved more than 1800 s in one frame, which forces a lighting refresh) | Update 0x197b277 |
| +0x1cc / +0x1c8 / +0x360 / +0x1d0 | | remap curve between clock time and internal time (100-entry table) | 0x197add0, 0x197a9e0 |
| +0x361 | byte | realtime mode (driven from UTC) | 0x197af90, 0x197a5d0 |

Methods (high):

* `SetTime(T, float seconds, int remap)` = **0x197aec0**: if `!T+0x25`, then `T.time = remap ? Remap(seconds) : seconds`; it also clears realtime.
* `float GetTime(T, int remap)` = **0x197a880**: `remap ? InverseRemap(T.time) : T.time`.
* `SetTarget(T, float seconds, int remap)` = 0x197afe0, and `SetTargetWithSpeed(T, seconds, remap, speed)` = 0x197b030.
  Both set has_target and **clear stopped**, so the clock runs to the target, and Update stops it there.
* `ClearTarget(T)` = 0x197a4e0. `SetRealtime` = 0x197af00 / 0x197af90. `EndNetworkSync` = 0x197a5d0.
* `Update(T, float dt, out)` = **0x197b170**. The part that matters:

```
197b1ac  cmp byte [rcx+0x24],0 ; jne skip          ; stopped?
197b1ba  time += dt * speed ; wrap into [0, 86400)
197b1e8  if (has_target && prev < target <= time) { time = target; stopped = 1; has_target = 0; speed_override = 0 }
197b211  if (!T+0x29) { prev = time; return 0 }   ; else evaluate the sky keyframes for `out`
```

## 2. The stateflow behaviours (media/stateflow.zip, global.xml)

The vtables come from RTTI. In every `States::*` behaviour vtable, slot 4 is the "enter" handler:

| state (global.xml) | behaviour | handler | what it does |
|---|---|---|---|
| `set_time_of_day` (hours, minutes, targeted, speed, realtime, gameoption) | `SetTimeOfDay` | 0x92f040 | `SetTime(T, h*3600+m*60, 1)`, or `SetTargetWithSpeed` when targeted, or realtime, or the game-options path |
| `set_time_of_day_from_gameoptions` | `SetTimeOfDayFromGameOptions` | 0x92f310 | reads the event's time option (gameoptions vf 0x110 / 0x120) and calls `SetTime` / `SetTarget` |
| `stop_time` / `start_time` | `StopTimeOfDay stopped=true/false` | 0x93c9c0 | `if (!T+0x27) T.stopped = stopped` |
| `clear_target_time_of_day` | `ClearTargetTimeOfDay` | 0x92e810 | `ClearTarget(T)` |
| `time_of_day_has_target_time` | `TimeOfDayHasTargetTime` | 0x946060 | branches on has_target |

Where the clock gets started and stopped (high, from the XML):

* **Free roam** (`free_roam_start.xml`, `finalise_free_roam_common`) runs `global.start_time` after streaming
  finishes. That is the only thing that keeps free-roam time moving. The first-time "IE" flows stop it until
  time of day has been "introduced" (`pause_time_and_weather_for_ie`).
* **Races** (`race_flow.xml`): `set_time_of_day_from_gameoptions`. If the event sets a fixed time, the race
  runs `stop_time`. A target time, or "Current", leaves the clock running. Later, `time_of_day_has_target_time` → `start_time`.
* **Car meets / road trips / E3 flows** use `stop_time` + `set_time_of_day hours=.. minutes=..`, and later `start_time`.
* **Online** uses `set_time_of_day realtime="true"` (network.xml).

Other code that sets time (medium): 0x408700 is an entity component. It runs SetTime from its own hours/minutes,
then sets `stopped` unless `+0x27` is set, which is the same set-and-hold idiom. 0x74c840 and 0x6f3ac0/0x6f7a70 are
`TimeOfDayComponent`/trigger property paths. Debug startup options `timeofdayindex` / `forcetimeofdayoverride`
(options +0x2ebc / +0x2ec0, parsed at 0x299d87) are applied at track load (0x2e59c3). They aren't reachable from
the retail UI.

## 3. Is there a hidden game option to surface?

No usable one. (medium-high)

* `EnumDataSource<TimeOfDayOptions>` (vtable 0x2e0e620, anonymous namespace) and the strings
  `UI/OnlineEventOptions/TimeOfDay/{Sunrise,Midday,Sunset,Midnight}` (registered at 0x147e30) and
  `IDS_TimeOfDay_*` make up the **Online Event Options** time list. A_OnlineEventOptions.xaml builds its rows from
  a generic `Options` data list. That list feeds `SetTimeOfDayFromGameOptions` at race start, not free roam.
* None of the Settings sub-pages (A_GameOptions, A_HudOptions, A_VideoOptions, A_AudioOptions, A_AssistOptions)
  bind anything time-related. Searching UI.zip for "TimeOfDay" finds no XAML at all.
* `SaveStates::GameTimeState` (float get/set, 0xa5fc10 / 0xa60060) is the save-game copy of the clock, not a setting.

## 4. How Time Lock holds the clock

Options considered:

1. **Per-frame memory poke of T+0x10.** This fights Update every frame and ignores the game's own stop/target logic. Rejected.
2. **Hook only `StopTimeOfDay`** and turn `start_time` into "set + stay stopped". This is clean, but it can't apply a
   change made in the Settings rows until the next start_time, and it can't catch clocks that start some other way.
3. **Hook `TimeOfDay::Update` entry (chosen).** This runs once per frame on the thread that owns the clock, the same
   point where the game advances time. The decision costs a few byte reads, and the clock is only written when
   something changes:
   * lock on, clock **running, no target, no +0x25/+0x27 lock** (free roam): `SetTime(T, hour*3600, 1)`,
     which is the game's own function with the same remap flag the stateflow passes, then `stopped = 1`. This is exactly
     `global.set_time_of_day` + `global.stop_time`.
   * our clock (still stopped, no target, and the time is still the exact value we set) and the hour changed in
     Settings: set it again.
   * lock turned off while our clock is still ours: `stopped = 0`, which is what `global.start_time` does.
   * anything the game stopped or retimed (race fixed time, cutscene, car meet) is left alone. When the game
     restarts the clock (free roam again), the hold applies again.
   * a new world (a different T pointer) forgets the old clock.

Guards: before anything is patched, these are compared byte for byte:

* Update's 11-byte prologue, and its advance block at 0x197b1ac (which proves the +0x10/+0x18/+0x24 layout)
* all of SetTime (61 bytes)
* GetTime (20 bytes)
* the StopTimeOfDay tail (`cmp [rax+0x27],0 / jne / mov [rax+0x24],bl`)

The hook itself is LiveTune's `entry_hook.c`. It uses a 16-byte-aligned entry and checks the prologue. It writes
a near stub and trampoline, and does one 8-byte store while the other threads are suspended. If anything doesn't
match, it logs and patches nothing.

Known effects and caveats (medium; none of these has been seen in game yet):

* The clock is stopped while it is held, so anything that waits for time to *pass* in free roam
  (TimeOfDayProgressTrigger / StrictTimeOfDayProgress components) won't fire. Turn the lock off if a story
  beat seems stuck.
* Races with their own time still use it. Free roam goes back to the locked hour when it restarts the clock.
* Extra Options' photo-mode time change lasts until the game restarts the clock (leaving photo mode); then the
  locked hour comes back.
* Online free roam is synced from realtime/the host. The lock will also hold that clock locally (untested).

## 5. The Settings entry (Settings > Video)

`ForzaUI.A_PauseOptions` is a **tile menu** (Assists, Controls, HUD, Audio, Video, Credits). LiveTune's crash
showed that every child of a tile Canvas must be a tile, so nothing is added there. The user chose the **Video**
sub-page (`ForzaUI.A_VideoOptions`). It is a plain `CommonListBox` named OptionsList with two rows: a Brightness
Slider (`Gamma`) and a Subtitles SpinnerControl (`OnOffOptions`). The list is `Width="806" Height="110"`.
(An earlier build used Settings > HUD. That edit has been removed completely, so Time Lock no longer touches
A_HudOptions, which the FH2 Mod Platform edits.)

* Page type: `VideoOptions` (vtable 0x2e60370, ctor 0xeaff10). Its property registry is **0x35470d0**, set up by
  **0xeca710**. That set-up registers Gamma (0xed6e90), Subtitles (0xed81f0), OnOffOptions (0xed77d0) and 0xed73c0
  through `REGISTER_PROPERTY` 0x155ee30 (which prepends to registry+0x40). Guard: `lea rcx,[0x35470d0]` at 0xeca808,
  then `call 0x155ee30` at 0xeca80f. (high)
* `OnOffOptions` lives at page+0x458 (getter 0xebc060). The ctor fills it at 0xeaffb5 with 0xc65df0, the same
  builder the HUD page uses: item 0 = `IDS_Misc_Off` (hash at 0x3549d8c, set at 0x165c40), item 1 = `IDS_Misc_On`.
  So index 0 = Off. (high)
* Templates, both taken from this page's own properties:
  * Subtitles is the int one (`type = 0x15c8b20(0)`, getter 0xebc8f0, setter 0xed0350, which reads boxed+0x28 as an int).
  * Gamma, the Brightness slider, is the float one (`type = 0x15c8c00(0)`; LiveTune uses the same one).
  * Both use property vtable 0x2c6f1e8. Boxing is `BOX_INT` 0x4702d0 / `BOX_FLOAT` 0x3f5990, and bindings are
    refreshed with 0xcf9c60(page, property). (high)
* Time Lock registers `TimeLockEnabled` (int) and `TimeLockHour` (float) on 0x35470d0. It then edits the page text
  in three ways:
  * Before the last `</ResourceDictionary>` ahead of the only `</Page.Resources>`, it inserts two
    `TileMenuDataItem`s, a `NumUnitConverter`, and a slider style based on `A_Style_Slider_Options` with its
    template copied, so the value shows as hours.
  * Before the closing tag of the only `x:Name="OptionsList"` CommonListBox (with no nested list), it inserts a
    SpinnerControl (`OnOffOptions`) and a Slider (0 to 23.75, step 0.25). They end up after Brightness, Subtitles
    and any rows other mods added.
  * It changes one existing attribute: if the OptionsList start tag still has the stock `Height="110"`, that
    becomes `Height="220"` so all four rows show. Any other height (Extra Options sets its own) is left alone,
    and the list scrolls.
* The page text is edited through Extra Options' resource jump at 0x13fe354, chained after EO and any other
  plugin, the same way as LiveTune's tuning_page.c. A page that already contains `{Binding TimeLockEnabled}` is
  left alone, and so is every other page.
* **Extra Options edits this page too.** Its DLL strings include `x:Class="ForzaUI.A_VideoOptions"`, the
  brightness-bar StackPanel (replaced with a centred `Width="806"` copy), `</ResourceDictionary>`,
  `<local:CommonListBox x:Name="OptionsList"` / `</local:CommonListBox>`, `Height="110"` → `Height="275"`, the
  description panel `Height="100"`, FOV sliders (`EO_FovSlider`, `EO_FovDegrees`) and `EO_RealtimeShadows` /
  `ToggleChoices` spinners. So in game, the text Time Lock receives is EO's output. The anchors above are all ones
  EO keeps. With `[Settings] SaveSettingsPageCopy=1` in TimeLock.ini (off by default in the release), the plugin
  also saves the page exactly as it receives it, once, to `seen_A_VideoOptions.xaml` in the mod folder, so the
  anchors can be checked against the real text.

## 6. Weather

Same method as for the clock: static reading, the stateflow XML and `media\weather\Presets*.xml`, all read only.
Addresses are RVAs. Each section below is high confidence unless it says otherwise.

### 6.1 The WeatherManager

* `world+0x488` = `Weather::WeatherManager` (vtable 0x2f64920). It sits next to the clock at `world+0x480`, and game
  code reaches it through `0x2e2e90(0x6ae290())`, which is `[[[0x34aba60]+0x48]+0x98]+0x488`. Its ctor is 0x197f060.
  It loads `media\weather\xboxone\weathermanager(_island).fxb` and `Presets.xml` / `Presets_Island.xml`.
* The world update (0x3005b0) updates the weather manager (0x19848b0 at 0x30071c, and 0x1985a10) on the same
  thread and in the same frame as `TimeOfDay::Update` (0x300933).
* Live values: `W+0x08` is atmospheric wetness (how cloudy or stormy the sky is), `W+0x18` is environmental wetness
  (how wet the road is), and `W+0x5c` is the precipitation level (rain).
* **Script control block at `W+0xe0`** (0x3c bytes), written only through
  **`WeatherManager::SetControl(W, &block)` = 0x1981860**:

| off | field |
|---|---|
| +0x00 / +0x04 | has preset / preset index (`FindPreset(W, std::string name)` = 0x1981e00; returns 0 if the name is unknown) |
| +0x08 / +0x0c | has severity / severity, enum `WeatherManager::Severity`: 0 Clear, 1 Cloudy, 2 Threatening, 3 Storm (registered at 0xf8d60) |
| +0x10 / +0x14 | has atmospheric wetness / value |
| +0x18 / +0x1c | has environmental wetness / value |
| +0x20 / +0x24 | has precipitation / value |
| +0x28 / +0x2c | has / value of a float written to `[W+0x198]` (no script sets it) |
| +0x30..+0x38 | the same constant tail in every caller: word 0, byte 1, float 1.0, byte 1 |

  SetControl keeps the previous block, so its changes are blended. A preset sets the severity, and the severity is turned
  into atmospheric wetness through the table at `W+0x1c0`. A block with **no "has" flag set means no control: dynamic
  weather**. `Release(W)` = **0x1981c80** builds exactly that block. `Pause(W)` = 0x1981bc0 sets the three wetness flags
  to the current values, which freezes the weather.
* Presets: on the mainland, Clear, CloudyA/B, ThreateningStorm{A,C}Cloudy{A,B} and StormBCloudy{A,B}. Storm Island
  adds CloudyFog, ThreateningFog and StormTropical{CloudyA,CloudyB,Fog}.
  `AllowedTransition` entries chain them: Clear → Cloudy → Threatening → Storm.

### 6.2 Stateflow behaviours (slot 4 of each `States::*` vtable)

| behaviour | handler | effect |
|---|---|---|
| `SetWeatherControl enable atmospheric_severity atmospheric_wetness environmental_wetness precipitation_level atmospheric_preset` | 0x92f750 | `enable`: builds the block from the given attributes (parameters at +0x68/+0xa8/+0xe8/+0x128/+0x168, registered at 0x929d2b), then SetControl. Otherwise Release. |
| `SetWeatherFromGameOptions enable` | 0x92f9b0 | If the event has custom weather (gameoptions vf 0x10), builds the block from the event's preset, wetness and rain values, then SetControl. **Otherwise Release.** |
| `PauseWeather pause gameoption` | 0x8e9a70 | `pause`: Pause(W). `unpause`: **Release(W)** |
| `EnableWeatherAutoChange`, `ApplyFullWeatherFromServer`, `WaitForFullWeatherReady` | | online sync (network.xml) |

All three of the first behaviours do nothing when `[0x2959a0()+0x3b7d]` is set (an options flag).

Where scripts use them:

* **Free roam:** `finalise_free_roam_common` runs `global.unpause_weather`, which is Release, so free roam runs on
  dynamic weather. The first-time flows pause the weather until it has been "introduced".
* **Races:** `set_race_weather enable=true` runs at pre-race. Fixed-weather races also run `pause_weather gameoption=true`.
  At the end, `set_race_weather enable=false`, which releases.
* **Car meets:** `SetWeatherControl` with Cloudy/CloudyA (or CloudyFog on the island), then `enable=false`.
* **Post-race cutscenes / IE island:** `SetWeatherControl` with presets and wetness values.
  TriggerWeatherComponent / WeatherProgressTrigger / StrictWeatherProgress components script weather in story beats
  (E3 and IE `WeatherSettings` files in GameTunableSettings.zip). Other SetControl callers are 0x2e52e3 (track load),
  0x40893b, 0x6c3822, 0x6f8433 and 0x173e71f. Other Release callers are 0x6eb35f and 0x174dfcc.

### 6.3 The game's own weather options (the menu list)

* `EnumDataSource<MainlandWeatherOptions>` (vtable 0x2e0e3c0) is made by **0xd61880** (ctor 0xd63bf0, 5 items) and
  backs the online event options. Its items are localized by 0xd72750: **0 `IDS_Weather_Current`, 1 Sunny, 2 Wet,
  3 Rain, 4 Storm**. `IslandWeatherOptions` (0xd63b20) adds Fog, HeavyFog and FoggyStorm.
* 0xd6f110 turns an option into the game's weather settings: a preset by name, a precipitation level and an
  environmental wetness, with the values taken from the `UI/OnlineEventOptions/Weather/*` tunables (their defaults
  are read at 0x1470xx–0x147dxx):

| option | preset | rain | road wetness |
|---|---|---|---|
| 1 Sunny | Clear | 0 | 0 |
| 2 Wet | ThreateningStormCCloudyA | 0 | 0.6 |
| 3 Fog (island) | CloudyFog | 0 | 0 |
| 4 HeavyFog (island) | ThreateningFog | 0.2 | 0.3 |
| 5 Rain | ThreateningStormACloudyB | 0.8 | 1.0 |
| 6 Storm | StormBCloudyA | 1.0 | 1.0 |
| 7 FoggyStorm (island) | StormTropicalFog | 1.0 | 1.0 |

  The game has no "Cloudy" option. Wet is the overcast and damp one, and the plain CloudyA preset is only used by
  car meets.

### 6.4 How Time Lock holds the weather

* **No new hook.** From the existing `TimeOfDay::Update` hook (same thread, right after the world update ran the
  weather manager), `weather_hold_frame` looks up W through the same pointer chain as the game's accessors, and only
  if `[world+0x480]` is the clock being updated. Each frame it reads the block at `W+0xe0`:
  * **Mode chosen and the block is released** (free roam, or right after a race, car meet or unpause releases it):
    `FindPreset(W, name)` with a correctly laid out MSVC `std::string` (inline for names under 16 characters, by
    pointer for longer ones), then `SetControl(W, &block)`. The block holds the preset, the road wetness and the
    rain level, with the standard tail. That is what `SetWeatherControl enable=true atmospheric_preset=…
    environmental_wetness=… precipitation_level=…` would do, using the game's own Sunny/Wet/Rain/Storm values.
    Time Lock then keeps a copy of W's block exactly as SetControl left it.
  * **The block is still exactly that copy, and the setting changed:** set the new mode, or call the game's
    `Release(W)` for Dynamic.
  * **Anything else** (a race's own weather, a car meet, a cutscene, pause) is left alone, and Dynamic never
    touches the game.
  * It skips everything while the game's own options flag (+0x3b7d) is set.
* Guards check, byte for byte:
  * the world accessor chain (0x6ae290, 0x21e0c0, 0x2e1520, 0x2e1f90, 0x2e2e90);
  * the weather update call in the world update (0x300715);
  * SetControl's prologue and its block copy;
  * Release's block build and its call to SetControl;
  * FindPreset's prologue and return;
  * the options accessor, and both behaviours' options checks.

  If anything differs, the weather hold is off and the log says why.
* Thread: SetControl is called on the world-update thread. The game's own SetControl callers are stateflow
  behaviours. From the call graph they appear to run on the game-logic thread that also runs the world update, but
  this has not been confirmed in game (medium).
* Not hooked: none of the functions FnFNitro / the FH2 Mod Platform hook (0x12C1EA0, 0xFA78A0, 0x384C00, 0x157FF60,
  0xAD6070, 0xDC8A60, 0xDC6E30). Time Lock hooks only 0x197b170.
* Caveats (untested): online free roam syncs weather from the host (`ApplyFullWeatherFromServer`) and may fight the
  hold. Story beats that script weather through TriggerWeatherComponent set flags, so they win while they last.
  Races without custom weather release the block, so they get the chosen weather too, the same way they get the
  locked hour.

### 6.5 The Weather row (Settings > Video)

* It is a third row after Locked Hour: `SpinnerControl Label="Weather" SelectedIndex="{Binding TimeLockWeather}"
  ItemsSource="{Binding TimeLockWeatherOptions}"`, with its own TileMenuDataItem description.
* `TimeLockWeather` is an int, a copy of the Subtitles template, with values 0 to 4 in MainlandWeatherOptions order.
* `TimeLockWeatherOptions` is a list property, a copy of this page's **OnOffOptions** property template (0xed77d0,
  the same kind of value). Its getter returns one MainlandWeatherOptions list built once by the game's 0xd61880.
  Time Lock keeps its own reference to that list forever and hands each caller another reference, the same shape
  as the game's list getters. So the row shows the game's localized "Current / Sunny / Wet / Rain / Storm", and
  Current is Dynamic.
* With the Weather row, the stock list height becomes 275 instead of 220 (5 rows). Any other height is left alone.
  The Weather row is only added when the weather hold's guards passed.
* ini: `[Weather] Mode=Dynamic|Sunny|Wet|Rain|Storm` (aliases Current and Clear). The default is Dynamic. Only the
  row writes Mode.

## 7. Offline verification (tests/offline_test.c, tests/run_tests.py)

The tests read the game files from your own install (`python tests/run_tests.py "<FH2 Mount folder>"`); none
are included in the repository.


* All the guards pass on the real EXE. Flipping any one checked byte makes each module refuse.
* The real hook was installed into a mapped copy of the EXE, and the **game's own** Update/SetTime/GetTime code
  ran on a fake clock (+0x29 = 0, no remap). Cases tested: free-roam hold (14:00 → 17:50, held for 100 frames);
  an hour change from Settings; race fixed time kept; targeted transition left running; restart re-held;
  stop-locked clock untouched; lock off resumes; lock off doesn't restart a game-stopped clock; midnight wrap.
* The XAML edit ran on the real A_VideoOptions.xaml from UI.zip, on a copy with EO-style edits reconstructed from
  its DLL strings, and on a copy edited by an imaginary other mod (an extra row, a resource with a nested
  ResourceDictionary, a different height). In all three:
  * the result is well-formed XML;
  * our resources are last in Page.Resources;
  * Brightness and Subtitles are still the first rows and ours are last;
  * the height went 110 → 220 only on the stock page (EO's 275 and the other mod's 165 were kept);
  * everything else is byte-for-byte unchanged, and a second pass is refused;
  * every StaticResource we reference is defined in the game's UI resources.
* The stock HUD page, a HUD page edited like the FH2 Mod Platform does, and the Audio and Pause pages are all left untouched.
* Weather: all the guards pass on the real EXE, and flipping any checked byte makes the weather hold refuse.
  * The control blocks match the game's option table (Rain = ThreateningStormACloudyB / road 1.0 / rain 0.8, and so on).
  * The frame logic was run against a fake weather manager whose SetControl and Release mimic the game's flag
    handling. Cases tested:
    * Dynamic never touches anything;
    * the mode is applied once and then held;
    * a change moves it;
    * weather the game set is left alone, even when the setting changes;
    * a release by the game re-applies the mode;
    * Dynamic calls the game's Release only on Time Lock's own block;
    * a clock from another world is ignored;
    * the ini is saved;
    * long and short preset names are passed correctly.
  * Each Video page variant also has the Weather row last, bound to `TimeLockWeatherOptions`.

Not verified (needs the game): the real SetControl blending on screen, the game's list rendering in the Weather row, the remap curve with real TOD data (the log shows the clock as read back through
the game's GetTime), the rows rendering and binding in the EO-edited Video page, and the effect on lighting and sky.
