#!/usr/bin/env python3
"""Writes the package's declarations: waterbox.config, file_slots.json and
default_keybinds.json.

The panels are the driver's (fbneo-driver.cpp, PanelNames): the same players,
the same button count, the same order. run-gate.sh holds the two together -
it asks the built core for its panel and compares it with what this wrote -
so a change to one without the other is a red gate, not a silent misbinding.

usage: waterbox/gen-config.py [OUTDIR]   (default: rewrites the three files beside it)
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
NEOGEO_SRC = os.path.join(HERE, "..", "extern", "FBNeo", "src", "burn", "drv", "neogeo", "d_neogeo.cpp")


def neogeo_bioses():
    """The Neo Geo's "BIOS" dip switch options, as FBNeo's shared Neo Geo list
    declares them - read from the source, so the setting follows upstream."""
    import re
    src = open(NEOGEO_SRC, encoding="latin-1").read()
    m = re.search(r'\{\s*0\s*,\s*0xFD\s*,\s*0\s*,\s*(\d+)\s*,\s*"BIOS"\s*\}', src)
    count = int(m.group(1))
    return re.findall(r'\{\s*0x02\s*,\s*0x01\s*,\s*0x3f\s*,\s*0x[0-9a-fA-F]+\s*,\s*"([^"]+)"', src[m.end():])[:count]

# id, label, setting value, players, buttons (0 = Neo Geo's A-D + Select),
# the picture's 4:3 virtual size
MACHINES = [
    ("CPS1", "Capcom CPS-1", "cps1", 4, 6, (384, 288)),
    ("CPS2", "Capcom CPS-2", "cps2", 4, 6, (384, 288)),
    ("CPS3", "Capcom CPS-3", "cps3", 2, 6, (384, 288)),
    ("NEOGEO", "SNK Neo Geo MVS", "neogeo", 2, 0, (320, 240)),
    ("SYS16", "Sega System 16", "system16", 4, 5, (320, 240)),
]


def panel(players, buttons):
    names = []
    for p in range(1, players + 1):
        P = f"P{p} "
        names += [P + d for d in ("Up", "Down", "Left", "Right")]
        if buttons:
            names += [f"{P}Button {b}" for b in range(1, buttons + 1)]
        else:
            names += [P + b for b in ("A", "B", "C", "D", "Select")]
        names += [P + "Start", P + "Coin"]
    return names + ["Service", "Test", "Reset"]


def axes(players):
    """Two analog axes per player (dials, trackballs, paddles), FBNeo's scale."""
    return [{"name": f"P{p} Axis {a}", "min": -1024, "max": 1023, "neutral": 0}
            for p in range(1, players + 1) for a in (1, 2)]


def controller_name(label):
    return label.replace("Capcom ", "").replace("SNK ", "").replace("Sega ", "") + " Panel"


def keybinds(players, buttons):
    """Player 1 on the keyboard and the first pad, players 2-4 on pads."""
    keys = {"Up": "Up", "Down": "Down", "Left": "Left", "Right": "Right"}
    pov = {"Up": "POV1U", "Down": "POV1D", "Left": "POV1L", "Right": "POV1R"}
    xdir = {"Up": "DpadUp", "Down": "DpadDown", "Left": "DpadLeft", "Right": "DpadRight"}
    xstick = {"Up": "LStickUp", "Down": "LStickDown", "Left": "LStickLeft", "Right": "LStickRight"}
    # six buttons as two rows of three, the way a Capcom panel is laid out
    kb_buttons = ["Z", "X", "C", "A", "S", "D"]
    pad_buttons = [("B1", "X"), ("B2", "A"), ("B3", "B"), ("B4", "Y"),
                   ("B5", "LeftShoulder"), ("B6", "RightShoulder")]
    out = {}
    for p in range(1, players + 1):
        P = f"P{p} "
        first = p == 1
        for d in ("Up", "Down", "Left", "Right"):
            parts = ([keys[d]] if first else []) + [f"J{p} {pov[d]}", f"X{p} {xdir[d]}", f"X{p} {xstick[d]}"]
            out[P + d] = ", ".join(parts)
        labels = [f"Button {b}" for b in range(1, buttons + 1)] if buttons else ["A", "B", "C", "D"]
        for i, lab in enumerate(labels):
            j, x = pad_buttons[i]
            parts = ([kb_buttons[i]] if first else []) + [f"J{p} {j}", f"X{p} {x}"]
            out[P + lab] = ", ".join(parts)
        if not buttons:
            out[P + "Select"] = ", ".join((["Q"] if first else []) + [f"J{p} B7", f"X{p} Back"])
        out[P + "Start"] = ", ".join(([f"D{p}"] if p <= 4 else []) + [f"J{p} B10", f"X{p} Start"])
        coin_pad = f"J{p} B9" + ("" if not buttons else f", X{p} Back")
        out[P + "Coin"] = ", ".join(([f"D{4 + p}"] if p <= 4 else []) + [coin_pad])
    out["Service"] = "D9"
    out["Test"] = "F2"
    out["Reset"] = ""
    return out


def main():
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else HERE
    machines = []
    binds = {}
    for mid, label, value, players, buttons, (vw, vh) in MACHINES:
        name = controller_name(label)
        comment = (
            f"{label}: {players} players, each "
            + (f"a stick and {buttons} buttons" if buttons else "a stick, A, B, C, D and Select")
            + ", then Start and Coin; then the cabinet's Service, Test and Reset. The buttons are "
            "the game's own in the order its FBNeo driver lists them - Street Fighter's Weak Punch "
            "to Strong Kick are Buttons 1-6, Magic Sword's Attack, Jump and Fire 3 Buttons 1-3 - "
            "and a control the game does not have leaves the input roll. Two analog axes per "
            "player carry a game's dial, trackball or paddle (-1024..1023, 0 at rest; a "
            "relative control takes the value as its speed this frame)."
        )
        machines.append({
            "id": mid,
            "label": label,
            "when": [value],
            "virtualWidth": vw,
            "virtualHeight": vh,
            "extensions": {".zip": mid} if mid == "CPS2" else {},
            "input": {"name": name, "_comment": comment, "buttons": panel(players, buttons),
                      "axes": axes(players)},
        })
        binds[name] = keybinds(players, buttons)

    config = {
        "coreName": "FBNeo",
        "author": "The FinalBurn Neo team; chimera port by Sergio Martin",
        "url": "https://github.com/ToolAssisted-run/chimera-core-fbneo",
        "machineSetting": "machine",
        "romFile": "romset",
        "deterministic": True,
        "suggestSettings": True,
        "memoryLayoutMiB": [64, 8, 8, 64, 768],
        "_memoryLayoutMiB_note": (
            "sbrk, sealed, invisible, plain, mmap. The boards themselves are small; the room is "
            "for their roms, which FBNeo holds in memory whole and decrypted - CPS-3's are 84 MB "
            "and its drivers keep a decrypted copy beside them."
        ),
        "video": {
            "_comment": (
                "buffer CAPACITY; the live size comes from GetVideoWidth/Height every frame. The "
                "widest picture is CPS-3's 496-pixel mode (Street Fighter III 2nd Impact); a "
                "vertical game is handed out upright, so its picture is taller than wide."
            ),
            "width": 512,
            "height": 512,
            "virtualWidth": 384,
            "virtualHeight": 288,
            "vsyncNumerator": 5963,
            "vsyncDenominator": 100,
        },
        "audio": {"samplesPerFrame": 2048, "channels": 2, "get": "GetAudio"},
        "lag": {"inputWasRead": "InputWasRead"},
        "machines": machines,
        "settings": [
            {
                "name": "machine",
                "display": "System",
                "type": "enum",
                "options": [m[2] for m in MACHINES],
                "default": "cps2",
                "description": (
                    "Which arcade board this project is: Capcom's CPS-1, CPS-2 or CPS-3, SNK's "
                    "Neo Geo MVS, or Sega's System 16 (16A and 16B). The game's rom set must be "
                    "one of that board's; a set for another board is a load error that names the "
                    "right one. Each board has its own control panel."
                ),
            },
            {
                "name": "neogeo_bios",
                "display": "Neo Geo BIOS",
                "type": "enum",
                "options": neogeo_bioses(),
                "default": neogeo_bioses()[0],
                "exposedWhen": {"setting": "machine", "is": "neogeo"},
                "description": (
                    "Which bios the Neo Geo boots: an MVS (arcade) bios of a region and version, "
                    "an AES (home console) bios, or a UniBIOS. The bios decides the region, the "
                    "language, and whether the game runs as an arcade or a home version, so it is "
                    "part of the machine. It must be in the project's neogeo.zip. The default is "
                    "FBNeo's own."
                ),
            },
            {
                "name": "cpu_clock",
                "display": "CPU Clock (%)",
                "type": "int",
                "default": 100,
                "min": 25,
                "max": 400,
                "exposedWhen": {"setting": "machine", "in": ["cps1", "cps2", "neogeo", "system16"]},
                "description": (
                    "The main CPU's clock, as a percentage of the board's own (FBNeo's CPU clock "
                    "setting). Above 100 a game slows down less; below, more. It changes what the "
                    "machine computes, so a movie needs the same value to play back. CPS-1, CPS-2, "
                    "Neo Geo and System 16 honour it; CPS-3 does not."
                ),
            },
            {
                "name": "force_60hz",
                "display": "Force 60 Hz",
                "type": "bool",
                "default": False,
                "description": (
                    "Runs a board whose refresh is near 60 Hz (the Neo Geo's 59.18, CPS-1/2's "
                    "59.63) at exactly 60. The game then runs that much faster against the clock "
                    "and makes a different sound, so it is part of the machine."
                ),
            },
            {
                "name": "socd",
                "display": "Opposite Directions",
                "type": "enum",
                "options": ["off", "neutral", "last-input-4way", "last-input-8way",
                            "first-input", "up-priority", "down-priority"],
                "default": "last-input-8way",
                "exposedWhen": {"setting": "machine", "in": ["cps1", "cps2", "cps3", "neogeo"]},
                "description": (
                    "What the game sees when a player holds opposite directions together (Left "
                    "and Right, Up and Down) - FBNeo's SOCD handling. 'off' passes both through, "
                    "as a stick wired straight to the board would if it could press both; "
                    "'neutral' cancels them; the others let one win (the last pressed, the "
                    "first, or up/down). A real joystick cannot press both, so the default "
                    "(FBNeo's own, last input, 8-way) is what a person with a stick can do; "
                    "'off' lets a movie do what no stick can."
                ),
            },
            {
                "name": "pcm_interpolation",
                "display": "Sample Interpolation",
                "type": "enum",
                "options": ["none", "2-point", "4-point"],
                "default": "2-point",
                "description": (
                    "How FBNeo resamples the boards' sample chips (QSound, the MSM6295 and "
                    "friends) to the output rate. Sound only: the machine is the same either "
                    "way, and a movie plays back with any value. The default is FBNeo's own."
                ),
            },
            {
                "name": "fm_interpolation",
                "display": "FM Interpolation",
                "type": "enum",
                "options": ["none", "2-point", "4-point"],
                "default": "none",
                "description": (
                    "How FBNeo resamples the boards' FM chips (YM2151, YM2610) to the output rate. "
                    "Sound only, like Sample Interpolation. The default is FBNeo's own."
                ),
            },
        ],
        "firmware": [
            {
                "id": "neogeo.zip",
                "display": "Neo Geo Bios Set",
                "description": (
                    "The Neo Geo's bios set, neogeo.zip as FBNeo names it (the MVS and AES bioses, "
                    "the Z80 bios, the fix-layer and zoom roms). Taken whole and read inside the "
                    "core, which picks the roms it needs by CRC. No size or hash is pinned because "
                    "the set is versioned by its contents."
                ),
                "name": "neogeo.zip",
                "requiredWhen": {"setting": "machine", "is": "neogeo"},
            }
        ],
    }
    with open(os.path.join(out, "waterbox.config"), "w") as f:
        json.dump(config, f, indent=2, ensure_ascii=True)
        f.write("\n")

    slots = {
        "_comment": "The rom sets a project takes. Slot ids are what project manifests record.",
        "slots": [
            {
                "id": "romset",
                "title": "Rom set",
                "min": 1,
                "max": 4,
                "formats": ["zip"],
                "help": (
                    "The game's FBNeo rom set first, named as FBNeo names it (ssf2t.zip) - the "
                    "name is how the core knows the game. A clone's set holds only what differs "
                    "from its parent, so add the parent's set after it (a clone of ssf2t needs "
                    "ssf2t.zip too). Every rom is found by its CRC, then its name."
                ),
            },
            {
                "id": "savedata",
                "title": "Save data",
                "min": 0,
                "max": 8,
                "formats": ["bin"],
                "help": (
                    "What a game keeps across power cycles, as Export Save Data wrote it: the "
                    "Neo Geo's NVRAM.bin and Memory_card.bin, a CPS-2 or CPS-3 EEPROM. Each file "
                    "goes back into the part of the machine it came from before the first frame."
                ),
            },
        ],
    }
    with open(os.path.join(out, "file_slots.json"), "w") as f:
        json.dump(slots, f, indent=2, ensure_ascii=True)
        f.write("\n")

    kb = {
        "_comment": [
            "Player 1 on the keyboard (arrows, Z X C / A S D for the six buttons, 1 Start, 5 Coin) "
            "and on the first pad; players 2-4 on pads 2-4 (their Start on 2-4, Coin on 6-8). "
            "The cabinet's Service is 9, Test F2; Reset ships unbound."
        ],
        "AllTrollers": binds,
        "AllTrollersAutoFire": {name: {} for name in binds},
        "AllTrollersAnalog": {name: {} for name in binds},
    }
    with open(os.path.join(out, "default_keybinds.json"), "w") as f:
        json.dump(kb, f, indent=1, ensure_ascii=True)
        f.write("\n")


if __name__ == "__main__":
    main()
