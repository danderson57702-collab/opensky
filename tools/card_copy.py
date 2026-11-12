#!/usr/bin/env python3
"""Copies pack/ files to the card that differ in size or are newer than the card's copy (macOS's
openrsync hung on the card mid-file, Oct 2026): each written beside its place, then put in place.
   tools/card_copy.py PACK CARD_DATA_DIR"""
import os, sys, shutil, time
src, dst = sys.argv[1], sys.argv[2]
todo = []
for root, dirs, files in os.walk(src):
    dirs[:] = [d for d in dirs if d != "saves"]
    for f in files:
        if f == "autotest.txt" or f.startswith("._") or f.startswith("sklt_") or f.startswith("opensky_"):
            continue
        s = os.path.join(root, f); d = os.path.join(dst, os.path.relpath(s, src))
        st = os.stat(s)
        try:
            dt = os.stat(d)
            if dt.st_size == st.st_size and dt.st_mtime >= st.st_mtime:
                continue
        except FileNotFoundError:
            pass
        todo.append((s, d, st.st_size))
total = sum(t[2] for t in todo); done = 0; t0 = time.time(); last = 0
print(f"{len(todo)} files, {total/1e9:.2f} GB to copy", flush=True)
for s, d, n in todo:
    os.makedirs(os.path.dirname(d), exist_ok=True)
    tmp = d + ".copying"
    with open(s, "rb") as a, open(tmp, "wb") as b:
        while True:
            buf = a.read(8 << 20)
            if not buf: break
            b.write(buf); done += len(buf)
            if time.time() - last > 30:
                last = time.time(); r = done / max(1, last - t0)
                print(f"{done/1e9:.2f} of {total/1e9:.2f} GB, {r/1e6:.0f} MB/s, ~{(total-done)/max(r,1)/60:.0f} min left", flush=True)
    os.replace(tmp, d)
print(f"copied {len(todo)} files, {done/1e9:.2f} GB in {(time.time()-t0)/60:.1f} min", flush=True)
