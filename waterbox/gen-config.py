#!/usr/bin/env python3
"""Writes the package's declarations: waterbox.config, file_slots.json and
default_keybinds.json.

The panels are the driver's (fbneo-driver.cpp, PanelNames): the same players,
the same button count, the same order. run-gate.sh holds the two together -
it asks the built core for its panel and compares it with what this wrote -
so a change to one without the other is a red gate, not a silent misbinding.

usage: waterbox/gen-config.py    (rewrites the three files beside it)
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))

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
            "and a control the game does not have leaves the input roll."
        )
        machines.append({
            "id": mid,
            "label": label,
            "when": [value],
            "virtualWidth": vw,
            "virtualHeight": vh,
            "extensions": {".zip": mid} if mid == "CPS2" else {},
            "input": {"name": name, "_comment": comment, "buttons": panel(players, buttons), "axes": []},
        })
        binds[name] = keybinds(players, buttons)

    config = {
        "coreName": "FBNeo",
        "author": "The FinalBurn Neo team; chimera port by Sergio Martin",
        "url": "https://github.com/ToolAssisted-run/chimera-core-fbneo",
        "machineSetting": "machine",
        "romFile": "romset",
        "deterministic": True,
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
            }
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
    with open(os.path.join(HERE, "waterbox.config"), "w") as f:
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
            }
        ],
    }
    with open(os.path.join(HERE, "file_slots.json"), "w") as f:
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
    with open(os.path.join(HERE, "default_keybinds.json"), "w") as f:
        json.dump(kb, f, indent=1, ensure_ascii=True)
        f.write("\n")


if __name__ == "__main__":
    main()
