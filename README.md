# chimera-core-fbneo

[FinalBurn Neo](https://github.com/finalburnneo/FBNeo)'s arcade boards as a
[Chimera](https://github.com/ToolAssisted-run/chimera) core, running in
miniBox's sandbox:

| System | Machine id | Players | Panel |
|---|---|---|---|
| Capcom CPS-1 | `CPS1` | 4 | stick, Buttons 1-6, Start, Coin |
| Capcom CPS-2 | `CPS2` | 4 | stick, Buttons 1-6, Start, Coin |
| Capcom CPS-3 | `CPS3` | 2 | stick, Buttons 1-6, Start, Coin |
| SNK Neo Geo MVS | `NEOGEO` | 2 | stick, A-D, Select, Start, Coin |
| Sega System 16 (16A, 16B) | `SYS16` | 4 | stick, Buttons 1-5, Start, Coin |

Every panel also has the cabinet's Service, Test and Reset. A game's buttons
are its own, in the order its FBNeo driver lists them (Street Fighter's Weak
Punch to Strong Kick are Buttons 1-6); a control the game does not have
leaves the input roll.

## What a project needs

- **The game's rom set**, named as FBNeo names it (`ssf2t.zip`): the name is
  how the core knows the game. A clone's set holds only what differs from its
  parent, so add the parent's set after it in the Rom set slot. Roms are found
  by CRC, then by name, so a set only has to hold the right bytes.
- **A Neo Geo game also needs the bios set**, `neogeo.zip`, as the project's
  firmware.

The package carries no rom.

## Building

```sh
git submodule update --init
meson setup build/meson-native -Dminibox_dir=$HOME/chimera/extern/chimera-common-minibox
ninja -C build/meson-native                   # run-native, run-wbx
./waterbox/build-package.sh                   # core.wbx -> ~/chimera/build/Cores/fbneo.chimeraCore
./waterbox/run-gate.sh                        # the gate
```

The gate's machine legs need rom sets in `tests/roms-local` (or `FBNEO_ROMS`):
`msword.zip`, `ssf2t.zip`, `sfiii3.zip`, `shinobi.zip`, `samsho4.zip` and
`neogeo.zip`. Without them it builds, checks the declarations, and says it
skipped every machine.

## Licence

This repository's own files are MIT (see `LICENSE`). FinalBurn Neo is under
its own licence (`extern/FBNeo/src/license.txt`), and so is the built core:
it may not be sold or used to seek money, changes to its source must be made
public, and a project that uses its source may not ask for donations. Parts
of FBNeo are MAME's and also under MAME's licence.
