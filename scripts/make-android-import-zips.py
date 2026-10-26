#!/usr/bin/env python3
"""Builds PvZ-datos.zip and PvZ-partida.zip for the Android resource importer, then checks
every entry against a copy of ResourceImportActivity's path logic (both the old importer and
the one with mods/ as a known top-level folder).

Usage: scripts/make-android-import-zips.py <output folder>

Reads the Mac install: the game data and the save from the desktop build's Application
Support folder, the mods from /Applications/Plants vs. Zombies.app."""
import os
import sys
import zipfile

HOME = os.path.expanduser("~")
BASE = os.path.join(HOME, "Library/Application Support/io.github.wszqkzqk/PvZPortable")
GAMEDATA = os.path.join(BASE, "gamedata")
USERDATA = os.path.join(BASE, "userdata")
MODS = "/Applications/Plants vs. Zombies.app/Contents/Resources/mods"
OUT = sys.argv[1]

SKIP_NAMES = {".DS_Store"}


def files_under(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            if name in SKIP_NAMES or name.startswith("._"):
                continue
            path = os.path.join(dirpath, name)
            yield path, os.path.relpath(path, root).replace(os.sep, "/")


def add(zf, path, arcname):
    zf.write(path, arcname, compress_type=zipfile.ZIP_DEFLATED)


# Data: one wrapper folder, which every version of the importer strips (README: "PvZ/main.pak").
data_zip = os.path.join(OUT, "PvZ-datos.zip")
with zipfile.ZipFile(data_zip, "w") as zf:
    add(zf, os.path.join(GAMEDATA, "main.pak"), "PvZ/main.pak")
    for path, rel in files_under(os.path.join(GAMEDATA, "properties")):
        add(zf, path, "PvZ/properties/" + rel)
    for path, rel in files_under(MODS):
        add(zf, path, "PvZ/mods/" + rel)

# Saves: same layout as the app's own export (userdata/...). No registry, no set-aside backups.
save_zip = os.path.join(OUT, "PvZ-partida.zip")
SAVE_FILES = ["users.dat", "user1.dat", "game1_0.v4"]
with zipfile.ZipFile(save_zip, "w") as zf:
    for name in SAVE_FILES:
        add(zf, os.path.join(USERDATA, name), "userdata/" + name)


# --- Python copies of ResourceImportActivity's logic ---
def is_known_top_level(name, with_mods):
    known = ("main.pak", "properties/", "Properties/", "data/", "images/", "particles/",
             "reanim/", "sounds/", "compiled/") + (("mods/",) if with_mods else ())
    return name.startswith(known)


def strip_common_prefix(name, with_mods):
    name = name.replace("\\", "/").lstrip("/")
    if is_known_top_level(name, with_mods):
        return name
    slash = name.find("/")
    if 0 < slash < len(name) - 1:
        return name[slash + 1:]
    return name


def save_dest(name):
    return name if name.startswith("userdata/") else "userdata/" + name


ok = True
with zipfile.ZipFile(data_zip) as zf:
    names = [i.filename for i in zf.infolist() if not i.is_dir()]
    for with_mods in (False, True):
        dests = [strip_common_prefix(n, with_mods) for n in names]
        expected = [n[len("PvZ/"):] for n in names]
        if dests != expected:
            ok = False
            print("data zip mismatch (with_mods=%s)" % with_mods)
    dests = [strip_common_prefix(n, True) for n in names]
    assert "main.pak" in dests
    assert any(d.startswith("properties/") for d in dests)
    mods = [d for d in dests if d.startswith("mods/")]
    print("data zip: %d entries, main.pak + %d properties + %d mods" % (
        len(dests), sum(d.startswith("properties/") for d in dests), len(mods)))
    print("  ->", ", ".join(dests))
    assert zf.testzip() is None

with zipfile.ZipFile(save_zip) as zf:
    names = [i.filename for i in zf.infolist() if not i.is_dir()]
    dests = [save_dest(n) for n in names]
    print("save zip:", ", ".join(dests))
    assert dests == ["userdata/" + n for n in SAVE_FILES]
    assert zf.testzip() is None

print("OK" if ok else "FAILED")
sys.exit(0 if ok else 1)
