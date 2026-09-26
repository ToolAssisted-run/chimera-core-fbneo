# chimera-core-fbneo: plan and decisions

## The survey (2026-09-26, upstream @ 6bde5e1)

Asked for: FBNeo's arcade systems, but only CPS-1, CPS-2, CPS-3, Neo Geo and
System 16.

- **Every CPU these five use is plain C**: Musashi 68000, the Z80, SH-2
  (CPS-3), the 8051 and 8039 (System 16's MCUs and sound). FBNeo's x86
  assembly 68000 (A68K) is Windows-32 only and is never built here.
- **No threads** in the library (thready.h is the frontend's rewind helper).
- **Two host inputs**, both switched off by one frontend hook:
  `BurnGetLocalTime` (the Neo Geo's uPD4990A calendar) and
  `BurnRandomInit` (a seed from `time(NULL)`). FBNeo already pins both when
  `is_netgame_or_recording()` says a recording is running - its own movie
  support. This core always says so, and its `MovieInfo` is the machine date,
  2000-01-01 00:00:00.
- **`USE_SPEEDHACKS` is Windows-only upstream.** The guest is always built on
  Linux, so every host runs the same machine.
- **Licence**: FBNeo's own (non-commercial) plus MAME's. It forbids asking
  for donations for a project that uses its source; Chimera asks for none.

## The build

Upstream's meson fragments are used as they are: each driver group's
`meson.build` names its drivers and the components (CPUs, devices, sound
chips) each needs, and `src/cpu`, `src/burn/devices` and `src/burn/snd` build
exactly those. What upstream's root adds - a frontend and its executable - is
left out; this core's root (`meson.build`) selects:

- capcom: `d_cps1.cpp`, `d_cps2.cpp`; cps3: `d_cps3.cpp`; neogeo:
  `d_neogeo.cpp`; sega: `d_sys16a.cpp`, `d_sys16b.cpp`.
- Sega's shared board code (`sys16_run.cpp`) calls into the System 18,
  Hang-On, OutRun, X-Board and Y-Board drivers, so those are compiled but
  **not listed** in `driverlist.h`: no game of theirs exists here.
- No frontend macro. `BUILD_SDL2` and friends switch on runahead, FLAC/MP3
  samples and the frontend's `MovieInfo`; the one thing the macro-less path
  needs is `<wchar.h>` for burn.cpp's title code, pre-included.
- `waterbox/frontend-stubs.cpp`: what FBNeo expects of a frontend and this
  core does not have - IPS patches, rom-data files, the Neo Geo CD, window
  re-initialisation. The frontend's folders (EEPROM, high scores, samples)
  point at a directory that does not exist, in both flavors: nothing is read
  from or written to the host, and a CPS-2 EEPROM starts as its driver's
  default.

## The driver (waterbox/fbneo-driver.cpp)

- **Game**: the Rom set slot's first file names the driver (`ssf2t.zip` ->
  `ssf2t`). The project's system must be the driver's (hardware prefix), or
  it is a load error that names the right board.
- **Roms**: FBNeo asks for each rom by index; it is found by CRC first (file
  names drift between set versions), then by any name the driver knows it by,
  in every archive given: the game's, a parent's, the Neo Geo bios set.
- **Picture**: 32-bit BGRA for every board but CPS-3, whose renderer writes
  16-bit pixels whatever it is told (`cps3run.cpp`); its palette is 5 bits a
  channel, so it draws at RGB565 and is widened losslessly. A vertical game is
  turned upright.
- **Sound**: 48 kHz stereo, FBNeo's `nBurnSoundLen` samples a frame.
- **Dip switches**: set to the driver's defaults at load, as FBNeo's own
  frontends do.
- **Panel**: see the README. Directions, Start, Coin and Select are bound by
  name; buttons by position among the player's other digital inputs; the
  cabinet's switches by the names drivers give them.
- **Memory domains**: each RAM and NVRAM area the driver's savestate scan
  declares, under its own name.
- **Turbo** is accepted and ignored: a driver's draw routine can be machine
  state, so FBNeo always draws.

## Measured (2026-09-26)

One game per system: Magic Sword (CPS-1), Super Street Fighter II Turbo
(CPS-2), Street Fighter III 3rd Strike (CPS-3), Shinobi (System 16A),
Samurai Shodown IV (Neo Geo, with neogeo.zip).

- native == sandbox over 1200 frames of exercised input, as a digest stream
  every 50 frames: all five.
- save/load around every frame, and a state reopened in a new host: all five
  unchanged. States: 1.5 MB (CPS-1/2, Neo Geo) to 6.9 MB (CPS-3, System 16).
- A coin gives a credit (SSF2T "CREDIT 1", Samurai Shodown IV "CREDIT 01",
  natively), and through the engine (chimera-run, a movie) it changes the
  picture where an idle movie does not.
- Chimera's package contract tests: pass.

## Open

- **Lag frames**: FBNeo reads a game's inputs through each board's own
  memory handlers; there is no common hook, so the core exports no
  `InputWasRead` yet.
- **Analog controls** (dials, trackballs, paddles on some CPS-1 and System 16
  games) are not on the panels.
- **Save data**: CPS-2's EEPROM and the Neo Geo's backup RAM and memory card
  live in the machine and its savestates; they are not exported as files.
- **Dip switches and the Neo Geo bios choice** as project settings.
- Only one game per system has been run, and no vertical game: which way
  one is turned upright (fbneo_frame) is unverified.
