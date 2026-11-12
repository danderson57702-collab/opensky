"""Packs the pack's many-file folders into one archive each, for the Vita.

The Vita's memory card file system finds a file by reading its folder's entries in turn, so
opening one of 11,000 meshes costs a scan of a huge folder (on the hardware, much of a
second); the engine's loading crawled. Each folder becomes one file the engine opens once,
reading its files by offset (engine/src/fileio.c). The loose files are left as they are (the
host tests and Vita3K still read them; the engine prefers the archive when it's there).

Usage: python3 pack_archives.py <pack dir>

PACK_DIR/<folder>.skpak (little-endian): "SKPK", u32 version (1), u32 file count, u32 index
bytes; per file, sorted by name (bytes): u32 offset (from the archive's start), u32 size,
u16 name length, the name (its path within the folder, '/'-separated, as the engine asks
for it); then the files' bytes, each at its offset (4-aligned).
"""
import os
import struct
import sys

FOLDERS = ("meshes", "objects", "terrain", "textures", "navmesh", "actors", "interiors", "creatures")
SKIP = ("_preview",)  # (the converter's own pictures: never read by the engine)


def pack_folder(pack, folder):
    root = os.path.join(pack, folder)
    names = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP and not d.startswith(".")]
        for f in filenames:
            if f.startswith("."):
                continue
            names.append(os.path.relpath(os.path.join(dirpath, f), root).replace(os.sep, "/"))
    names.sort(key=lambda n: n.encode())
    sizes = [os.path.getsize(os.path.join(root, n)) for n in names]
    index_bytes = sum(10 + len(n.encode()) for n in names)
    offset = (16 + index_bytes + 3) & ~3
    offsets = []
    for size in sizes:
        offsets.append(offset)
        offset = (offset + size + 3) & ~3
    if offset >= 1 << 32:
        raise SystemExit(f"{folder}: {offset} bytes - too big for one archive")
    out_path = os.path.join(pack, folder + ".skpak")
    with open(out_path + ".tmp", "wb") as out:
        out.write(struct.pack("<4sIII", b"SKPK", 1, len(names), index_bytes))
        for n, off, size in zip(names, offsets, sizes):
            nb = n.encode()
            out.write(struct.pack("<IIH", off, size, len(nb)) + nb)
        for n, off in zip(names, offsets):
            out.write(b"\0" * (off - out.tell()))
            with open(os.path.join(root, n), "rb") as f:
                out.write(f.read())
    os.replace(out_path + ".tmp", out_path)
    print(f"archives: {folder}.skpak {len(names)} files, {offset / 1e6:.0f} MB")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    for folder in FOLDERS:
        if os.path.isdir(os.path.join(sys.argv[1], folder)):
            pack_folder(sys.argv[1], folder)


if __name__ == "__main__":
    main()
