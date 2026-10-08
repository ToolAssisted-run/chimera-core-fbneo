# chimera-core-fbneo

[FinalBurn Neo](https://github.com/finalburnneo/FBNeo)'s arcade boards, and
the Neo Geo CD, as a [Chimera](https://github.com/ToolAssisted-run/chimera)
core, running in miniBox's sandbox:

| System | Machine id | Players | Panel |
|---|---|---|---|
| Capcom CPS-1 | `CPS1` | 4 | stick, Buttons 1-6, Start, Coin |
| Capcom CPS-2 | `CPS2` | 4 | stick, Buttons 1-6, Start, Coin |
| Capcom CPS-3 | `CPS3` | 2 | stick, Buttons 1-6, Start, Coin |
| SNK Neo Geo MVS | `NEOGEO` | 2 | stick, A-D, Select, Start, Coin |
| Sega System 16 (16A, 16B) | `SYS16` | 4 | stick, Buttons 1-5, Start, Coin |
| SNK Neo Geo CD | `NEOCD` | 2 | stick, A-D, Select, Start |

Every arcade panel also has the cabinet's Service, Test and Reset, and two
analog axes per player for a game's dial, trackball or paddle; the Neo Geo CD
is a console, with its Reset and nothing else. A game's buttons are
its own, in the order its FBNeo driver lists them (Street Fighter's Weak
Punch to Strong Kick are Buttons 1-6); a control the game does not have
leaves the input roll. Lag frames are counted: a frame in which the game did
not read its controls.

## Settings

- **The game's dip switches**, one setting each (`dip.<switch>`), shown by
  the new-project wizard once it knows the game and by the settings of a
  loaded project. Defaults are the driver's. A CPS-2 game has none: it keeps
  its settings in its EEPROM, set in its service menu.
- **Neo Geo BIOS**: every MVS, AES and UniBIOS FBNeo knows (the bios must be
  in the project's `neogeo.zip`).
- **The Neo Geo CD's switches**, the same way (`dip.Region`, `dip.BIOS`,
  `dip.CD Loading Speed`): FBNeo's default loads faster than the console
  did; `Normal` is the console's own speed.
- **CPU Clock (%)**: FBNeo's overclock (CPS-1, CPS-2, Neo Geo, System 16).
- **Force 60 Hz**: runs a near-60 Hz board at exactly 60.
- **Opposite Directions**: FBNeo's SOCD handling of Left+Right and Up+Down
  held together. The default is FBNeo's (last input wins, 8-way); `off`
  passes both through to the game.
- **Sample / FM Interpolation**: sound quality only; a movie plays back with
  any value.

Everything but the two interpolation settings is part of the machine: a movie
needs the same value to play back.

## Save data

What a game keeps across power cycles - the Neo Geo's NVRAM and memory card,
a CPS-2 or CPS-3 EEPROM, a System 16 board's battery RAM - exports as one
file per part (`NVRAM.bin`, `Memory_card.bin`, ...), and a project's Save
data slot puts them back before the first frame.

## What a project needs

- **The game's rom set**, named as FBNeo names it (`ssf2t.zip`): the name is
  how the core knows the game. A clone's set holds only what differs from its
  parent, so add the parent's set after it in the Rom set slot. Roms are found
  by CRC, then by name, so a set only has to hold the right bytes.
- **A Neo Geo game also needs the bios set**, `neogeo.zip`, as the project's
  firmware.
- **A Neo Geo CD game is a disc image** in the Disc slot: a `.cue` sheet with
  its track files beside it, or a `.chd`. Its firmware is two bios sets,
  `neocdz.zip` and `neogeo.zip`.

The package carries no rom and no disc.

## Using it in Chimera

Chimera includes no cores and downloads none. Download
`fbneo-<version>.chimeraCore` from this repository's
[Releases](https://github.com/ToolAssisted-run/chimera-core-fbneo/releases)
page, or build it, and put it in the `Cores` folder beside `Chimera.exe`.
File > Core Manager lists that folder and can point Chimera at another. The
same file works on Linux and on Windows.

## Building

```sh
git submodule update --init
meson setup build/meson-native -Dminibox_dir=$HOME/chimera/extern/chimera-common-minibox
ninja -C build/meson-native                   # run-native, run-wbx
./waterbox/build-package.sh                   # core.wbx -> ~/chimera/build/Cores/fbneo.chimeraCore
./waterbox/run-gate.sh                        # the gate
```

As written, these expect a Chimera checkout at `~/chimera` with miniBox
built; anywhere else, pass `-Dminibox_dir=<miniBox>`, `-m <miniBox>` and
`-r <chimera>`. [docs/BUILDING.md](docs/BUILDING.md) has every step;
[AGENTS.md](AGENTS.md) is the short version for an AI coding agent.

The gate's machine legs need rom sets in `tests/roms-local` (or `FBNEO_ROMS`):
`msword.zip`, `ssf2t.zip`, `sfiii3.zip`, `shinobi.zip`, `samsho4.zip` and
`neogeo.zip`; the Neo Geo CD's need a disc (`FBNEO_NEOCD=<its .cue>`, or one
in `tests/roms-local/neocd`) and `neocdz.zip`. Without them it builds, checks
the declarations, and says it skipped every machine.

## Licence

This repository's own files are MIT (see `LICENSE`). FinalBurn Neo is under
its own licence (`extern/FBNeo/src/license.txt`), and so is the built core:
it may not be sold or used to seek money, changes to its source must be made
public, and a project that uses its source may not ask for donations. Parts
of FBNeo are MAME's and also under MAME's licence.
