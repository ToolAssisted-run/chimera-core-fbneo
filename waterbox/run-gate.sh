#!/bin/sh
# The FBNeo core's gate.
#
# Written against ~/chimera/docs/gates.md, and the ways a gate has already been
# watched to go green on a broken thing:
#
#   * a DEAD machine finishes instantly and every digest agrees with itself,
#     so the machine legs assert the picture is alive first;
#   * an END-STATE digest misses a machine that ran differently and arrived
#     at the same place, so flavors are compared as a per-frame digest STREAM;
#   * a guest build that failed was silently packaged as the PREVIOUS binary,
#     so the package leg checks core.wbx is newer than its sources.
#
# The machine legs need rom sets, which cannot ship: put them (or links to
# them) in tests/roms-local, or point FBNEO_ROMS at a folder holding them. A
# system with no set there is SKIPPED, visibly; a gate that ran no machine at
# all still proves the build and the declarations.
#
# Usage: ./run-gate.sh [-r <chimera root>] [-m <minibox dir>]
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
chimera_root="${CHIMERA_ROOT:-$HOME/chimera}"
mb="${MINIBOX_DIR:-$chimera_root/extern/chimera-common-minibox}"
roms="${FBNEO_ROMS:-$root/tests/roms-local}"
while getopts "r:m:" opt; do
	case "$opt" in
		r) chimera_root="$OPTARG" ;;
		m) mb="$OPTARG" ;;
		*) exit 2 ;;
	esac
done

work="$root/build/gate"
rm -rf "$work"; mkdir -p "$work"
native="$root/build/meson-native/run-native"
wbxrun="$root/build/meson-native/run-wbx"
core="$root/build/meson-guest/core.wbx"
run="$chimera_root/build/meson-linux/chimera-run"
pkg="$chimera_root/build/Cores/fbneo.chimeraCore"

pass=0; fail=0; skip=0
report() {
	case "$1" in
		PASS) pass=$((pass+1)) ;;
		FAIL) fail=$((fail+1)) ;;
		SKIP) skip=$((skip+1)) ;;
	esac
	printf '%-6s %-44s %s\n' "$1" "$2" "${3:-}"
}
finish() {
	printf '\n%d passed, %d failed, %d skipped\n' "$pass" "$fail" "$skip"
	[ "$pass" -eq 0 ] && { echo "NOTHING RAN"; exit 1; }
	[ "$fail" -eq 0 ] || exit 1
	exit 0
}

# ---------------------------------------------------------------- 1. build
if [ ! -f "$root/build/meson-native/build.ninja" ]; then
	meson setup "$root/build/meson-native" "$root" -Dminibox_dir="$mb" > "$work/setup.log" 2>&1 || true
fi
if ninja -C "$root/build/meson-native" > "$work/native.log" 2>&1; then
	report PASS "the native reference and harness build"
else
	report FAIL "the native reference and harness build" "see build/gate/native.log"
fi
if sh "$here/build-package.sh" -r "$chimera_root" -m "$mb" > "$work/package.log" 2>&1; then
	report PASS "package builds" "$(grep -o 'sha1 [0-9a-f]*' "$work/package.log")"
else
	report FAIL "package builds" "see build/gate/package.log"
fi
stale=""
for src in "$here"/fbneo-driver.cpp "$here"/wbx-entry.cpp "$here"/frontend-stubs.cpp "$here"/zip-reader.cpp "$root"/meson.build; do
	[ "$core" -nt "$src" ] || stale="$stale $(basename "$src")"
done
if [ -f "$core" ] && [ -z "$stale" ]; then
	report PASS "core.wbx is not stale"
else
	report FAIL "core.wbx is not stale" "older than:$stale"
fi

# The declarations are what gen-config.py writes: nobody hand-edits one of the
# three files and leaves the generator (and the panel it mirrors) behind.
mkdir -p "$work/gen"
cp "$here/gen-config.py" "$work/gen/"
python3 "$work/gen/gen-config.py"
drift=""
for f in waterbox.config file_slots.json default_keybinds.json; do
	cmp -s "$work/gen/$f" "$here/$f" || drift="$drift $f"
done
if [ -z "$drift" ]; then
	report PASS "the declarations are gen-config.py's"
else
	report FAIL "the declarations are gen-config.py's" "differs:$drift (run waterbox/gen-config.py)"
fi

# ---------------------------------------------------------- 2. the machines
# system, its rom set's file name, the panel's controller name
sets="cps1:msword.zip cps2:ssf2t.zip cps3:sfiii3.zip system16:shinobi.zip neogeo:samsho4.zip"

# workdir <dir> <system> <set> [bios]: the files a project would mount
workdir() {
	d="$1"; rm -rf "$d"; mkdir -p "$d"
	ln -s "$roms/$3" "$d/$3"
	[ -n "${4:-}" ] && ln -s "$roms/$4" "$d/$4"
	printf '{"machine":"%s"}' "$2" > "$d/settings"
	printf '{"romset":["%s"]}' "$3" > "$d/slots"
}

stream() { awk '/^frame/' "$1"; }

ran_any=0
for entry in $sets; do
	sys="${entry%%:*}"; set_="${entry#*:}"
	if [ ! -f "$roms/$set_" ]; then
		report SKIP "$sys: every machine leg" "no $set_ in $roms"
		continue
	fi
	bios=""
	if [ "$sys" = neogeo ]; then
		if [ ! -f "$roms/neogeo.zip" ]; then
			report SKIP "$sys: every machine leg" "no neogeo.zip (the bios set) in $roms"
			continue
		fi
		bios=neogeo.zip
	fi
	ran_any=1
	w="$work/$sys"
	workdir "$w" "$sys" "$set_" "$bios"

	# the panel the core binds is the panel the package declares
	"$wbxrun" "$core" "$w" --frames 1 --list-panel > "$work/$sys.panel" 2>&1 || true
	python3 - "$here/waterbox.config" "$sys" "$work/$sys.panel" > "$work/$sys.panelcheck" 2>&1 <<'PY' && ok=1 || ok=0
import json, re, sys
cfg = json.load(open(sys.argv[1]))
m = [m for m in cfg["machines"] if sys.argv[2] in m["when"]][0]
got = [re.match(r"panel \d+ '(.*)' ", l).group(1) for l in open(sys.argv[3]) if l.startswith("panel ")]
if got != m["input"]["buttons"]:
    sys.exit(f"core: {got}\ndeclared: {m['input']['buttons']}")
print(f"{len(got)} controls, {sum(1 for l in open(sys.argv[3]) if l.rstrip().endswith('active'))} live")
PY
	if [ "$ok" = 1 ]; then
		report PASS "$sys: the panel is the declared one" "$(cat "$work/$sys.panelcheck")"
	else
		report FAIL "$sys: the panel is the declared one" "see build/gate/$sys.panelcheck"
	fi

	# native == sandbox, over an exercised run, as a stream
	"$native" "$w" --frames 1200 --report 50 --exercise > "$work/$sys.n" 2>"$work/$sys.ne" || true
	"$wbxrun" "$core" "$w" --frames 1200 --report 50 --exercise > "$work/$sys.w" 2>"$work/$sys.we" || true
	pics="$(stream "$work/$sys.n" | awk '{print $7}' | sort -u | wc -l)"
	if [ "$(stream "$work/$sys.n" | wc -l)" -ne 24 ] || [ "$pics" -lt 3 ]; then
		report FAIL "$sys: the machine is alive" "$(stream "$work/$sys.n" | wc -l) reports, $pics distinct pictures"
	elif ! cmp -s "$work/$sys.n" "$work/$sys.w"; then
		report FAIL "$sys: native == sandbox (1200 frames, exercised)" \
			"first difference: $(diff "$work/$sys.n" "$work/$sys.w" | awk 'NR==2' | cut -c1-60)"
	else
		report PASS "$sys: native == sandbox (1200 frames, exercised)" "$pics distinct pictures"
	fi

	# the input reaches the machine: an exercised run is not an idle one
	"$wbxrun" "$core" "$w" --frames 1200 --report 50 > "$work/$sys.idle" 2>/dev/null || true
	if [ -s "$work/$sys.idle" ] && ! cmp -s "$work/$sys.idle" "$work/$sys.w"; then
		report PASS "$sys: the input reaches the machine" "negative control: idle differs"
	else
		report FAIL "$sys: the input reaches the machine" "an exercised run digests like an idle one"
	fi

	# savestates: around every frame, and across a new host
	"$wbxrun" "$core" "$w" --frames 600 --report 50 --exercise > "$work/$sys.p" 2>/dev/null || true
	"$wbxrun" "$core" "$w" --frames 600 --report 50 --exercise --rerecord > "$work/$sys.r" 2>"$work/$sys.re" || true
	"$wbxrun" "$core" "$w" --frames 600 --report 50 --exercise --session > "$work/$sys.s" 2>/dev/null || true
	if [ -s "$work/$sys.p" ] && cmp -s "$work/$sys.p" "$work/$sys.r"; then
		report PASS "$sys: rerecord changes nothing" "$(grep -o 'stateBytes=[0-9]*' "$work/$sys.re")"
	else
		report FAIL "$sys: rerecord changes nothing"
	fi
	if [ -s "$work/$sys.p" ] && cmp -s "$work/$sys.p" "$work/$sys.s"; then
		report PASS "$sys: a state reopens in a new host"
	else
		report FAIL "$sys: a state reopens in a new host"
	fi

	# the wrong system is a load error that names the right one
	other=cps1; [ "$sys" = cps1 ] && other=cps2
	printf '{"machine":"%s"}' "$other" > "$w/settings"
	"$wbxrun" "$core" "$w" --frames 1 > "$work/$sys.wrong" 2>&1 || true
	printf '{"machine":"%s"}' "$sys" > "$w/settings"
	if grep -q "the project says" "$work/$sys.wrong" && grep -q "game - pick" "$work/$sys.wrong"; then
		report PASS "$sys: a project for another board refuses it" "$(grep -o 'is a [A-Za-z0-9 -]* game' "$work/$sys.wrong")"
	else
		report FAIL "$sys: a project for another board refuses it" "$(tail -1 "$work/$sys.wrong")"
	fi

	# through the engine: the package in chimera-run, a movie of the live panel
	if [ -x "$run" ] && [ -f "$pkg" ]; then
		python3 - "$work/$sys.panel" "$work/$sys.movie" <<'PY'
import re, sys
live = [re.match(r"panel \d+ '(.*)' (\S+)", l).groups() for l in open(sys.argv[1]) if l.startswith("panel ")]
live = [n for n, a in live if a == "active"]
def player(n):
    m = re.match(r"P(\d+) ", n)
    return int(m.group(1)) if m else 0
groups = max(player(n) for n in live) + 1
coin = next(i for i, n in enumerate([n for n in live if player(n) == 1]) if n == "P1 Coin")
out = []
for f in range(1600):
    parts = []
    for g in range(groups):
        cells = ["." for n in live if player(n) == g]
        if g == 1 and 1500 <= f < 1510:
            cells[coin] = "C"
        parts.append("".join(cells))
    out.append("|" + "|".join(parts) + "|\n")
open(sys.argv[2], "w").write("".join(out))
PY
		fw=""; [ -n "$bios" ] && fw="--firmware neogeo.zip=$roms/neogeo.zip"
		# shellcheck disable=SC2086
		if "$run" "$pkg" "$roms/$set_" "$work/$sys.movie" --settings "{\"machine\":\"$sys\"}" $fw \
			> "$work/$sys.engine" 2>&1 \
			&& grep -q '^frames=1600' "$work/$sys.engine"; then
			report PASS "$sys: runs 1600 frames in the engine" "a coin at 1500"
		else
			report FAIL "$sys: runs 1600 frames in the engine" "see build/gate/$sys.engine"
		fi
	else
		report SKIP "$sys: runs in the engine" "no chimera-run or package"
	fi
done

# The Neo Geo's bios set is firmware: without it the load error says so.
if [ -f "$roms/samsho4.zip" ]; then
	workdir "$work/nobios" neogeo samsho4.zip
	"$wbxrun" "$core" "$work/nobios" --frames 1 > "$work/nobios.log" 2>&1 || true
	if grep -q "needs its bios set, neogeo.zip" "$work/nobios.log"; then
		report PASS "neogeo: no bios set is a load error naming it"
	else
		report FAIL "neogeo: no bios set is a load error naming it" "$(tail -1 "$work/nobios.log")"
	fi
fi
[ "$ran_any" = 1 ] || report SKIP "every machine leg" "no rom set in $roms (FBNEO_ROMS)"

finish
