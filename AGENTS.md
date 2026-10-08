# AGENTS.md - FBNeo core for Chimera

This repository builds FinalBurn Neo's arcade boards as a core for Chimera
(https://github.com/ToolAssisted-run/chimera), a frontend for tool-assisted
speedruns. It produces one file, `fbneo.chimeraCore`: the emulator as a
sandboxed guest (`core.wbx`) plus the declarations Chimera reads. One
package holds six systems: CPS-1, CPS-2, CPS-3, Neo Geo MVS, System 16 and
the Neo Geo CD (whose game is a disc image, not a rom set).
Upstream FBNeo is a submodule; everything else here is the driver, the
build and the gate.

`<chimera>` is a Chimera checkout and `<miniBox>` is its submodule
`<chimera>/extern/chimera-common-minibox`.

## Layout

- `extern/FBNeo/` - upstream FinalBurn Neo, a pinned git submodule.
- `patches/` - the numbered patch series over the submodule (one patch:
  a game reading its controls is marked, for lag frames). `patches/README.md`.
- `meson.build` - both builds: the native reference and, as a cross build,
  the guest. It picks the driver files and applies the patches when it
  configures.
- `waterbox/fbneo-driver.{cpp,h}` - the adapter around FBNeo: picks the
  game's driver, feeds it its roms, steps one frame. In both builds.
- `waterbox/wbx-entry.cpp` - the guest ABI over the driver.
- `waterbox/frontend-stubs.cpp` - what FBNeo expects of a frontend.
- `waterbox/zip-reader.{cpp,h}` - reads the rom sets.
- `waterbox/gen-config.py` - WRITES `waterbox.config`, `file_slots.json`
  and `default_keybinds.json`. Those three are generated.
- `waterbox/run-native.c`, `run-wbx.c`, `gate-harness.h` - the two gate
  drivers and the options and schedule they share.
- `waterbox/package-licenses.json` - the licences that travel in the package.
- `waterbox/setup-guest.sh`, `build-package.sh`, `apply-patches.sh` - build.
- `waterbox/run-gate.sh` - the gate. It builds, packages, then tests; its
  work files and logs go to `build/gate/`.
- `tests/roms-local/` - your own rom sets for the gate. Gitignored.
- `docs/PLAN.md` - decisions and measurements.

## Set up the build environment

    sudo apt-get update
    sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libx11-dev libxext-dev libasound2-dev

    git submodule update --init
    git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>

    mb=<miniBox>
    [ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
    meson compile -C "$mb/build/meson-linux"
    [ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
    meson compile -C "$mb/build/meson-cpp"

That is the workflow's package list and its miniBox build. `perl` must be
installed too: `meson.build` requires it. The `meson-cpp` build downloads
the GCC source matching the host `gcc`, once. If a Chimera checkout already
exists, use it and skip the clone.

`build-package.sh` and the gate's own legs use miniBox only. The legs that
run the package in Chimera's engine, and Chimera's contract tests, need
Chimera built as well, with the .NET SDK 8.0: the commands are in
`docs/BUILDING.md`.

## Build

    ./waterbox/build-package.sh -m <miniBox> -r <chimera>

This configures and builds the guest (`build/meson-guest/core.wbx`), checks
it with miniBox's `check-wbx.sh` and writes
`<chimera>/build/Cores/fbneo.chimeraCore`. The native reference and the
sandbox driver, for debugging and for the gate:

    meson setup build/meson-native -Dminibox_dir=<miniBox>
    ninja -C build/meson-native

A hand-built package stamps `<commit>+local` (`-dirty` with changes in the
tree). It is for testing. CI stamps the commit through `CORE_VERSION`.

## Install the core into Chimera

Chimera ships no cores and downloads none. `build-package.sh -r <chimera>`
writes the package into `<chimera>/build/Cores/`, which is the cores folder
of a source checkout. For a release bundle, copy `fbneo.chimeraCore` into
the `Cores` folder beside `Chimera.exe` (or the folder set in File > Core
Manager > Change folder...). File > Core Manager lists the folder; Refresh
List rescans it. The same file works on Linux and on Windows.

## Test before you commit

    CHIMERA_ROOT=<chimera> MINIBOX_DIR=<miniBox> ./waterbox/run-gate.sh

It must end with `0 failed`. The gate rebuilds the native reference and
the package itself, so it also replaces the package in `<chimera>`. Logs
are in `build/gate/`.

Read the skip count. With no rom set the gate runs four legs (the two
builds, `core.wbx is not stale`, the declarations are the generator's) and
skips every leg that runs a machine. That is what CI does, because no rom
set may be on a public runner. It says nothing about emulation. The machine
legs are run where the rom sets are, before anything is pushed:
`tests/roms-local`, or `FBNEO_ROMS=<folder>`. The file names are in
`docs/BUILDING.md`.

CI also runs Chimera's contract tests on the package (`docs/BUILDING.md`,
"Chimera's contract tests"). They need Chimera built and no rom set.

## Rules of this repository

- **Upstream is not edited in place.** `extern/FBNeo` is a submodule pinned
  to upstream. A change to it is a numbered patch in `patches/`.
  `meson.build` runs `waterbox/apply-patches.sh` on every configure; the
  script accepts a pristine tree or one that carries the whole series, and
  stops on anything in between. Never commit inside the submodule.
  `git status` shows it modified once the patches are applied; that is
  expected.
- **Do not hand-edit the three generated files.** Change
  `waterbox/gen-config.py` and run it. The gate fails when
  `waterbox.config`, `file_slots.json` or `default_keybinds.json` is not
  what the generator writes.
- **The panel is declared twice and must agree.** `PanelNames` in
  `fbneo-driver.cpp` and the panels in `gen-config.py` list the same
  controls in the same order. That order is what a movie is written in. The
  gate compares them, but only for a system whose rom set is present.
- **Determinism is the product.** The guest must not read host time, host
  randomness or anything else that differs between runs. FBNeo's clock and
  random seed are pinned here; nothing is read from or written to the host.
  A savestate must round-trip. The gate checks it; a change that breaks it
  is a bug.
- **Run the gate before committing.** A new leg needs a negative control:
  show that it fails when the thing it checks is broken, and say so in the
  commit. `run-gate.sh` opens with the ways a gate has gone green on a
  broken thing; read it before adding a leg.
- **Never commit rom sets, bios sets or save data.** The package carries no
  rom. Never add network access.
- **The licence binds the build.** The built core is under FBNeo's
  non-commercial licence (`README.md`, `waterbox/package-licenses.json`).
  Code compiled in must be declared in `package-licenses.json`.
- **Shell scripts stay executable** (git mode 100755), and so does
  `gen-config.py`.
- **Documentation prose is plain ASCII.**
- **Commit messages.** The subject is `type(scope): ` plus a sentence that
  states what is now true ("fix(video): vertical games are upright, and say
  they are 3:4"); some subjects are the sentence alone. The body gives the
  cause, the change, and the gate result with any new legs.
- **Do not edit `.github/workflows`** unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md` - every build step, option and error message.
- `docs/PLAN.md` - what is built, why, and what was measured.
- `.github/workflows/chimera.yml` - the recipe CI runs; it is authoritative.
- `README.md` - the systems, the settings, what a project needs.
- In Chimera: `docs/porting-a-core.md`, `docs/gates.md`,
  `docs/core-manager.md`.
