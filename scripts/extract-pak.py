#!/usr/bin/env python3
"""Extract a PvZ GOTY main.pak into a folder of loose files for sprite, sound and text mods.

The game looks inside main.pak first and falls back to loose files, so run it with
-resdir pointing at the extracted folder (with no main.pak in it) to use edited files.
"""
import argparse
import shutil
from pathlib import Path


def read_xor(file, size):
    data = file.read(size)
    if len(data) != size:
        raise ValueError("unexpected end of main.pak")
    return bytes(byte ^ 0xF7 for byte in data)


def read_records(pak):
    if read_xor(pak, 4) != b"\xc0\x4a\xc0\xba":
        raise ValueError("invalid main.pak signature")
    if int.from_bytes(read_xor(pak, 4), "little") != 0:
        raise ValueError("unsupported main.pak version")

    records = []
    while True:
        flags = read_xor(pak, 1)[0]
        if flags & 0x80:
            break
        name_width = read_xor(pak, 1)[0]
        name = read_xor(pak, name_width).decode("utf-8")
        size = int.from_bytes(read_xor(pak, 4), "little")
        read_xor(pak, 8)  # file time
        records.append((name.replace("\\", "/"), size))
    return records


def extract(pak_path, out_dir):
    out_dir = out_dir.resolve()
    with pak_path.open("rb") as pak:
        records = read_records(pak)
        for name, size in records:
            target = (out_dir / name).resolve()
            if out_dir not in target.parents:
                raise ValueError(f"unsafe path in main.pak: {name}")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(read_xor(pak, size))
    return len(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("main_pak", type=Path, help="path to main.pak")
    parser.add_argument("output", type=Path, help="folder to extract into")
    parser.add_argument("--properties", type=Path,
                        help="properties/ folder to copy next to the extracted files (default: beside main.pak)")
    args = parser.parse_args()

    count = extract(args.main_pak, args.output)
    properties = args.properties or args.main_pak.parent / "properties"
    if properties.is_dir():
        shutil.copytree(properties, args.output / "properties", dirs_exist_ok=True)
    print(f"Extracted {count} files to {args.output}")


if __name__ == "__main__":
    main()
