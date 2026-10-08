#!/usr/bin/env python3
"""Writes a .chimeraProject for one of this package's machines, the way the
wizard would: the files in their slots, a value for every setting the machine
shows, the firmware its conditions call for, and an input log in the machine's
own control names.

usage: make-project.py <package> <out.chimeraProject> <frames> <machine value>
                       [<setting>=<value> ...] [press=<control>:<first>:<count> ...]
                       -- <slot=file> [<slot=file> ...]

A .cue brings the track files it names with it, as the wizard does: each one
joins the project in the "support" slot, from the cue's own folder. The
firmware sha1s are left empty - a bios set is pinned by nothing but its name
(waterbox.config) and chimera-run takes --firmware <id>=<path> for the file.
"""
import hashlib
import json
import os
import re
import sys
import zipfile


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 24), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def holds(cond, settings):
    """the condition language of docs/project.md, the setting tests only"""
    if cond is None:
        return True
    if "all" in cond:
        return all(holds(c, settings) for c in cond["all"])
    if "any" in cond:
        return any(holds(c, settings) for c in cond["any"])
    if "not" in cond:
        return not holds(cond["not"], settings)
    if "setting" in cond:
        have = settings.get(cond["setting"])
        if "is" in cond:
            return have == cond["is"]
        return have in cond.get("in", [])
    return False


def player_of(name):
    return int(name[1]) if len(name) > 2 and name[0] == "P" and name[1].isdigit() else 0


def main():
    package, out, frames, machine = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
    rest = sys.argv[5:]
    sep = rest.index("--")
    options, slotargs = rest[:sep], rest[sep + 1:]

    z = zipfile.ZipFile(package)
    cfg = json.loads(z.read("waterbox.config"))
    mcfg = next(m for m in cfg["machines"] if machine in m["when"])
    buttons = mcfg["input"]["buttons"]
    axes = mcfg["input"].get("axes", [])

    presses = {}
    overrides = []
    for o in options:
        k, v = o.split("=", 1)
        if k == "press":
            control, first, count = v.rsplit(":", 2)
            presses[control] = (int(first), int(count))
        else:
            overrides.append((k, v))

    groups = max([player_of(a["name"]) for a in axes] + [player_of(b) for b in buttons] + [0]) + 1
    key = ""
    for g in range(groups):
        key += "#"
        for a in axes:
            if player_of(a["name"]) == g:
                key += a["name"] + "|"
        for b in buttons:
            if player_of(b) == g:
                key += b + "|"
    rows = []
    for f in range(frames):
        row = ""
        for g in range(groups):
            row += "|"
            for a in axes:
                if player_of(a["name"]) == g:
                    row += "%5d," % a.get("neutral", 0)
            for b in buttons:
                if player_of(b) == g:
                    first, count = presses.get(b, (0, 0))
                    row += "X" if first <= f < first + count else "."
        rows.append(row + "|")
    log = "[Input]\nLogKey:" + key + "\n" + "\n".join(rows) + "\n[/Input]\n"

    files = []
    for arg in slotargs:
        slot, path = arg.split("=", 1)
        files.append({"name": os.path.basename(path), "sha1": sha1(path), "slot": slot})
        if path.lower().endswith(".cue"):
            for line in open(path, encoding="utf-8", errors="replace"):
                m = re.match(r'\s*FILE\s+"(.*)"', line)
                if m and not any(f["name"] == m.group(1) for f in files):
                    track = os.path.join(os.path.dirname(path), m.group(1))
                    files.append({"name": m.group(1), "sha1": sha1(track), "slot": "support"})

    settings = {cfg["machineSetting"]: machine}
    for d in cfg.get("settings", []):
        if d["name"] not in settings and d.get("default") is not None and holds(d.get("exposedWhen"), settings):
            settings[d["name"]] = d["default"]
    for k, v in overrides:
        settings[k] = v == "true" if v in ("true", "false") else int(v) if v.lstrip("-").isdigit() else v

    firmware = [{"id": d["id"], "sha1": d.get("sha1", "")}
                for d in cfg.get("firmware", []) if holds(d.get("requiredWhen"), settings)]

    project = {
        "id": "fbneo-gate-%s" % machine,
        "title": "%s through Chimera" % mcfg["label"],
        "description": "written by waterbox/tests/make-project.py",
        "core": {"name": cfg["coreName"], "version": cfg["version"], "sha1": sha1(package)},
        "rerecords": 0,
        "files": files,
        "settings": settings,
        "firmware": firmware,
        "coreCache": [],
        "input": log,
        "markers": [],
        "branches": [],
        "headers": {
            "MovieVersion": "Chimera Project File v1.1",
            "Platform": mcfg["id"],
            "SHA1": files[0]["sha1"] if files else "",
            "LastInputFrame": str(frames - 1),
            "VsyncNumerator": str(cfg["video"]["vsyncNumerator"]),
            "VsyncDenominator": str(cfg["video"]["vsyncDenominator"]),
        },
    }
    with open(out, "w") as f:
        json.dump(project, f, indent="\t")
    return 0


if __name__ == "__main__":
    sys.exit(main())
