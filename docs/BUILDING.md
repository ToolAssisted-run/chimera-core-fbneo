# Building the FBNeo core

This builds one file, `fbneo.chimeraCore`: FinalBurn Neo's CPS-1, CPS-2,
CPS-3, Neo Geo and System 16 boards as a sandboxed guest (`core.wbx`) with
its declarations, which Chimera loads. The steps are the ones
`.github/workflows/chimera.yml` runs from a fresh clone on a public Ubuntu
runner. That workflow is the reference: when this page and the workflow
disagree, the workflow is right.

`<chimera>` below is a checkout of
https://github.com/ToolAssisted-run/chimera, and `<miniBox>` is its
submodule `<chimera>/extern/chimera-common-minibox`.

## Requirements

- Linux, x86-64. CI builds on GitHub's `ubuntu-latest`. The build runs on
  Linux; the package it makes is the same file on Linux and on Windows.
- The packages the workflow's one job installs:

      sudo apt-get update
      sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libx11-dev libxext-dev libasound2-dev

  No compiler version is pinned: the build uses the `gcc` and `g++` that
  `build-essential` installs. The same job builds Chimera; this
  repository's own build files name none of `cmake`, `pkg-config`,
  `mono-complete`, `xvfb` or the `-dev` libraries.
- `perl`: this repository's `meson.build` requires it, for upstream's
  generators. `git` and `curl` are used too (by the scripts and by miniBox's
  build). The workflow installs none of the three, so they have to be there
  already.
- The .NET SDK 8.0, for building Chimera and running its contract tests. The
  workflow gets it from `actions/setup-dotnet@v4` with
  `dotnet-version: '8.0'`; by hand, Chimera's README gives

      curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0

- The network, once: miniBox's C++ guest toolchain downloads the GCC source
  that matches the host `gcc` (about 84 MB) and builds libstdc++ for the
  guest from it. Nothing in this repository downloads anything.

## Get the sources

This repository, with its one submodule (`extern/FBNeo`, upstream FinalBurn
Neo). The workflow uses `actions/checkout@v6` with `submodules: true`,
which is:

    git clone https://github.com/ToolAssisted-run/chimera-core-fbneo.git
    cd chimera-core-fbneo
    git submodule update --init

A Chimera checkout. The workflow checks out Chimera's `main` with every
submodule (`submodules: recursive`):

    git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>

Where the scripts look when they are not told:

| Script | Option | Default |
| --- | --- | --- |
| `meson setup` (`meson.build`) | `-Dminibox_dir=<miniBox>` | `../chimera/extern/chimera-common-minibox`, beside this repository; an error if it is not there |
| `waterbox/setup-guest.sh` | `-m <miniBox>` or `MINIBOX_DIR` | `$HOME/chimera/extern/chimera-common-minibox` |
| `waterbox/build-package.sh` | `-r <chimera>` | `../chimera` beside this repository, then `$HOME/chimera` |
| `waterbox/build-package.sh` | `-m <miniBox>` or `MINIBOX_DIR` | `<chimera>/extern/chimera-common-minibox` |
| `waterbox/run-gate.sh` | `-r <chimera>` or `CHIMERA_ROOT` | `$HOME/chimera` |
| `waterbox/run-gate.sh` | `-m <miniBox>` or `MINIBOX_DIR` | `<chimera>/extern/chimera-common-minibox` |
| `waterbox/run-gate.sh` | `FBNEO_ROMS` | `tests/roms-local` |

The defaults are not all the same place. Pass the paths, as the workflow
does.

## Build miniBox

The sandbox host, and the guest toolchain with C++ (FBNeo is C and C++):

    mb=<miniBox>
    [ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
    meson compile -C "$mb/build/meson-linux"
    [ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
    meson compile -C "$mb/build/meson-cpp"

`build/meson-linux` holds the host library the sandbox driver links
(`source/host/libminiboxhost.so`). `build/meson-cpp` holds the guest sysroot
(`guest-sysroot/`, musl and libstdc++) the core is compiled against. The
workflow caches these two directories with `actions/cache@v4`; on your own
machine they simply stay where they are.

## Build the core

In CI there is no separate build step: `waterbox/run-gate.sh` builds the
native reference and the package itself (see "Run the gates"). By hand the
same three things are:

**Patches.** `patches/` holds the series over `extern/FBNeo`: one patch,
which marks a game reading its controls (that is how lag frames are
counted). There is no separate step: `meson.build` runs
`waterbox/apply-patches.sh` every time it configures. The script judges the
series as a whole:

- a pristine submodule gets every patch (`applied: <name>`);
- a submodule that already carries the whole series is left alone
  (`already applied: all N patches`);
- anything in between is an error that names the files, and so is a series
  that does not apply to the submodule's HEAD.

Once the patches are applied, `git status` shows `extern/FBNeo` as
modified. That is the patch set; it is not committed.

**The native reference and the sandbox driver.**

    meson setup build/meson-native -Dminibox_dir=<miniBox>
    ninja -C build/meson-native

This builds two programs in `build/meson-native`:

- `run-native`: the same FBNeo sources and the same driver as the core,
  built for the host with no sandbox. It is what the gate compares the core
  against, and where debugging is done.
- `run-wbx`: runs `core.wbx` through the miniBox host, with the same
  schedule and the same digests as `run-native`.

**The guest.** `waterbox/build-package.sh` configures and builds it. To
build it without packaging:

    MINIBOX_DIR=<miniBox> sh waterbox/setup-guest.sh
    ninja -C build/meson-guest core.wbx

`waterbox/setup-guest.sh [-m <miniBox dir>]` writes the meson cross file
`build/guest-cross.ini` (machine-local paths, not committed) and configures
`build/meson-guest`. Arguments after the options go to `meson setup`. It
stops if `<miniBox>/build/meson-cpp` has no guest sysroot. The result is
`build/meson-guest/core.wbx`.

Only the five systems are built. `meson.build` selects the driver files
(`d_cps1.cpp`, `d_cps2.cpp`, `d_cps3.cpp`, `d_neogeo.cpp`, `d_sys16a.cpp`,
`d_sys16b.cpp`) and upstream's own meson fragments build the components
those drivers ask for.

## Build the package

    ./waterbox/build-package.sh -m <miniBox> -r <chimera>

Usage: `./build-package.sh [-m <miniBox dir>] [-r <chimera root>]`. There is
no output-directory option. The script:

1. configures the guest build if `build/meson-guest` is not configured, and
   runs `ninja -C build/meson-guest core.wbx`;
2. runs miniBox's `source/guest/check-wbx.sh` on `core.wbx` and stops if the
   guest is not sandbox-clean;
3. stages `core.wbx`, `waterbox.config`, `default_keybinds.json`,
   `file_slots.json`, a `licenses/` folder (the licence texts
   `waterbox/package-licenses.json` names) and a `build.json` (what built
   it) in `build/package-staging`;
4. stamps the version into the staged `waterbox.config`;
5. writes `<chimera>/build/Cores/fbneo.chimeraCore`, packs it a second time
   and stops if the two are not byte-identical. It prints
   `package sha1 <hash>` and `packaged -> <path>`;
6. removes any `<chimera>/build/CoreCache/fbneo-*` directory.

It needs a built miniBox. It does not need a built Chimera.

**The version** is the commit. CI passes `CORE_VERSION` (the commit SHA) to
the gate, which passes it on. Without it the script stamps `<commit>+local`,
where `<commit>` is twelve characters, or `<commit>-dirty+local` when
`git diff --quiet HEAD` reports a change. The applied patch set counts as a
change (the submodule reads as modified), so a hand build normally says
`-dirty`. `versionDate` is the commit's date in UTC, never the build's. A
hand-built package is for testing: Chimera's publish step refuses a version
with `+local` or `-dirty`.

CI publishes what passed the gate: a rolling `dev` release on every green
push to `main`, and a dated `nightly-YYYY-MM-DD` release from the scheduled
run (cron `0 4 * * *`), only when `main` moved since the last one. Nothing
is published from a pull request. The asset is named
`fbneo-<version>.chimeraCore`.

## Install it into Chimera

Chimera ships no cores and downloads none: it has no network code. A core
gets there as a file.

- **A Chimera source checkout.** The cores folder is `<chimera>/build/Cores/`
  and `build-package.sh -r <chimera>` has already written the package there.
  Start Chimera (`<chimera>/build/ChimeraMono.sh` on Linux).
- **A release bundle.** Copy `fbneo.chimeraCore` into the `Cores` folder
  beside `Chimera.exe`, or into the folder chosen in File > Core Manager >
  Change folder... The same file serves a Linux and a Windows Chimera.

File > Core Manager lists the packages in that folder; Refresh List rescans
it. A hand-built version reads as its commit followed by `local`.

To use a published build instead, download `fbneo-<version>.chimeraCore`
from https://github.com/ToolAssisted-run/chimera-core-fbneo/releases and
put it in the same folder.

## Run the gates

### The core gate

    CHIMERA_ROOT=<chimera> MINIBOX_DIR=<miniBox> ./waterbox/run-gate.sh

Usage: `./run-gate.sh [-r <chimera root>] [-m <minibox dir>]`. The
environment form above is what the workflow runs. The gate builds what it
tests: it configures `build/meson-native` if needed, runs `ninja` there,
and runs `build-package.sh -r <chimera> -m <miniBox>`. Running it therefore
replaces `<chimera>/build/Cores/fbneo.chimeraCore`. It needs miniBox built.
Its work files and logs are in `build/gate/`, which it empties first.

With nothing provided, four legs run:

- `the native reference and harness build`;
- `package builds`;
- `core.wbx is not stale`: `core.wbx` is newer than the driver sources and
  `meson.build`, so a failed guest build was not packaged as the previous
  binary;
- `the declarations are gen-config.py's`: `waterbox.config`,
  `file_slots.json` and `default_keybinds.json` are exactly what
  `waterbox/gen-config.py` writes.

Every leg that runs a machine is then reported `SKIP`, with the rom set it
wanted. This is all a public runner can do: no rom set may be put on one.
The last line is `<n> passed, <n> failed, <n> skipped`. The exit status is
non-zero on any failure, and on `NOTHING RAN` (no leg passed).

The machine legs need rom sets, which are not distributed. Put your own in
`tests/roms-local` (gitignored), or point `FBNEO_ROMS` at a folder that
holds them, under the names FBNeo gives them:

| File | What it unlocks |
| --- | --- |
| `msword.zip` | the CPS-1 legs, and the per-game settings leg |
| `ssf2t.zip` | the CPS-2 legs, and the CPU clock leg |
| `sfiii3.zip` | the CPS-3 legs |
| `shinobi.zip` | the System 16 legs, and the dip switch legs |
| `samsho4.zip` with `neogeo.zip` | the Neo Geo legs: bios setting, Force 60 Hz, interpolation, opposite directions, save data |
| `forgottn.zip` | the analog axis legs |
| `varth.zip` | the vertical picture legs |

A system whose set is missing is skipped by name; the others still run. For
each system present the gate proves: the panel the core binds is the one
the package declares; the machine is alive and the native reference and the
sandbox print the same digest stream over 1200 exercised frames; an idle
run differs (the input reached the machine); a savestate around every frame
changes nothing; a state reopens in a new host; a project for another board
is refused with a message naming the right one; lag frames are counted.

Some legs run the package through Chimera's engine. They need
`<chimera>/build/meson-linux/chimera-run`, which Chimera's native build
makes (next section); without it they report `SKIP`, with the reason
`no chimera-run or package`.

### Chimera's contract tests

The workflow then runs Chimera's own tests against the package the gate
installed. They need Chimera's natives and the managed solution built
first, as the workflow builds them:

    cd <chimera>
    meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
    meson compile -C build/meson-linux
    meson install -C build/meson-linux
    dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false

In the workflow these come before the gate, so `chimera-run` exists when
the gate runs. Then:

    cd <chimera>
    CHIMERA_CORES_DIR=<chimera>/build/Cores dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
      -c Release --nologo \
      --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"

They prove the package is readable, is built for a guest ABI this frontend
runs, becomes a working factory, binds only buttons its controllers
declare, stamps a version, and gives no two controls of one controller the
same movie letter. They need no rom set. They run over every package in
`CHIMERA_CORES_DIR`.

There is no separate frontend gate script in this repository.

## Files the core needs at run time

None of these is in the repository or in the package. The user provides
them.

| What | Where it goes | Notes |
| --- | --- | --- |
| The game's rom set, a `.zip` named as FBNeo names it | the Rom set slot, first (1 to 4 files) | The name is how the core knows the game. A clone's set holds only what differs from its parent: add the parent's set after it. Roms are found by CRC, then by name. |
| `neogeo.zip`, the Neo Geo bios set | project firmware | Needed when the System setting is `neogeo`. No size or hash is pinned. |
| Save data, `.bin` files as Export Save Data wrote them | the Save data slot (0 to 8 files) | Optional. Each goes back into the part of the machine it came from before the first frame. |

The System setting (`cps1`, `cps2`, `cps3`, `neogeo`, `system16`) must be
the board of the game's set; a set for another board is a load error that
names the right one.

## Troubleshooting

- **`pass -Dminibox_dir=<miniBox checkout>`** from `meson setup`: there is
  no Chimera checkout at `../chimera`. Pass the option.
- **`meson setup` stops because it cannot find `perl`**: `meson.build`
  requires it. Install it.
- **`miniBox C++ guest toolchain missing under <miniBox>/build/meson-cpp`**
  from `setup-guest.sh`: miniBox was built without `-Dguest_cpp=true`. The
  message gives the command.
- **`chimera checkout not found; pass -r <path>`** from `build-package.sh`:
  pass `-r`.
- **`FAIL the native reference and harness build` / `FAIL package builds`**:
  the gate hides the build output. Read `build/gate/setup.log`,
  `build/gate/native.log` and `build/gate/package.log`.
- **`FAIL core.wbx is not stale`**: `core.wbx` is older than one of the
  named sources. The guest build did not produce a new binary; read
  `build/gate/package.log`.
- **`FAIL the declarations are gen-config.py's`**: one of the three
  generated files was edited by hand, or the generator changed and was not
  run. Run `waterbox/gen-config.py`.
- **`extern/FBNeo is not checked out`**: the submodule is empty. The message
  gives the command.
- **`extern/FBNeo is partly patched`**: some file the series touches is
  neither pristine nor as the whole series leaves it. The message names the
  files and gives the command that starts again from the submodule's HEAD.
  That command discards edits made in the tree: turn them into a patch
  first.
- **`the series does not apply to the submodule's HEAD at <patch>`**: the
  submodule was moved without rebasing the patches.
- **A green gate with every machine skipped** proves the build and the
  declarations, nothing about emulation. The machine legs have to be run
  where the rom sets are, before anything is pushed.
- **A moved miniBox.** `build/guest-cross.ini` holds absolute paths. Run
  `waterbox/setup-guest.sh` again after miniBox moves.
