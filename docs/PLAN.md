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

## Settings, lag, save data (2026-09-26, second round)

Discovered by reading FBNeo's globals and each driver's dip lists, and
decided with the user:

- **Dip switches are per game** (Magic Sword 13 groups, Shinobi 8, CPS-3 a
  Region and "Less sprite lag", SSF2T none - CPS-2 keeps its settings in
  EEPROM). Chimera declares settings once per package, so Chimera learned
  per-game settings: the core's `GetGameSettings` export (after Init) and a
  `settings` array in its `SuggestSettings` answer (before, in the wizard).
  Each group is `dip.<group>`, its options the driver's names, its default
  the driver's; duplicate group or option names get " #2". The Neo Geo's
  BIOS group is left to the board setting.
- **Applying switches**: after the driver starts. Some are read only at
  reset (the Neo Geo takes its bios then), so a machine whose switches
  differ from the driver's defaults holds the driver's own Reset input on its
  first frame; one on the defaults boots exactly as FBNeo boots it.
- **Board settings**: `neogeo_bios` (the 35 options read from
  d_neogeo.cpp by gen-config.py), `cpu_clock` (nBurnCPUSpeedAdjust; CPS-3
  ignores it), `force_60hz` (bForce60Hz), `socd` (nSocd - found on the way:
  FBNeo cleans opposite directions by default, last input wins 8-way, and
  `off` hands both to the game), `pcm_interpolation` / `fm_interpolation`
  (nInterpolation / nFMInterpolation, output only - measured: same RAM and
  lag, other sound). Blending (bBurnUseBlend) needs FBNeo's blend-table
  files, which a sandbox never has, so it is not offered.
- **Lag** (patch 0001): `CHIMERA_INPUT_READ` marks a board's control reads -
  CpsReadPort's player and coin ports (not 0x021, which also carries the
  EEPROM bit read constantly), CPS-3's four input words, the Neo Geo's input
  bank, System 16's per-game handlers - and the driver clears the flag
  before each frame. Counted, 1200 exercised frames: CPS-1 115, CPS-2 48,
  CPS-3 69, System 16 3, Neo Geo 285, native == sandbox.
- **Save data**: every area the driver scans as NVRAM, memory card or
  EEPROM (ACB_EEPROM: FBNeo keeps EEPROMs out of its own states and writes
  them to files, which this core never reads or writes) is one exported
  file; the Save data slot puts each back before the first frame, and a file
  the game has no area for, or of another size, is a load error. Neo Geo
  round trip: identical bytes, and a different machine from a fresh boot.
- **Analog**: two axes a player, -1024..1023 (FBNeo's scale), bound to the
  player's analog inputs in driver order. Forgotten Worlds (CPS-1) binds its
  rotary aim to four axes (P1/P2 Aim X, Y); holding P1 Aim X from frame 1000
  aims the soldier and scores 1600 by frame 1500 where the still run scores
  0; native == sandbox == rerecord (gate).

## Open

- **Vertical games are untried.** 1944 turned out to be a horizontal game
  (384x224, not flagged vertical); FBNeo's vertical parents here are 1941,
  Mercs and Varth (CPS-1), 19XX and Dimahoo (CPS-2).
- Only one game per system has been run (two on CPS-1 and CPS-2), and no
  vertical game: which way one is turned upright (fbneo_frame) is
  unverified.
