"""Converts Skyrim's exploration music (xWMA in "Skyrim - Sounds.bsa") to .skmus.

Usage: python3 extract_music.py <pack dir>

Writes <pack>/music/<name>.skmus and music.txt (the playlist). Like everything in the
pack, the output is converted from the user's own game files and must not be shared.

.skmus (little-endian): "SKMU", u16 version, u16 channels, u32 sample rate, u32 frames,
u32 block_frames; then blocks, each holding every channel's IMA ADPCM block in turn
(4-byte header: s16 predictor, u8 step index, u8 0; then block_frames / 2 bytes).
"""
import ctypes
import io
import os
import struct
import subprocess
import sys

import av
import numpy as np

from bsa import BSA
from extract_textures import SKYRIM_DATA

SKMU_VERSION = 1
RATE = 32000
BLOCK_FRAMES = 2048
PEAK = 0.9  # normalize every track to this peak (Skyrim's masters are quiet and uneven)
MAX_GAIN = 2.5

TRACKS = [f"music\\explore\\mus_explore_day_{i:02d}.xwm" for i in range(1, 10)] + \
         [f"music\\explore\\mus_explore_morning_{i:02d}.xwm" for i in range(1, 3)] + \
         [f"music\\explore\\mus_explore_dusk_{i:02d}.xwm" for i in range(1, 4)] + \
         [f"music\\explore\\mus_explore_night_{i:02d}.xwm" for i in range(1, 7)]

# Set pieces played on their own (music_special), not in the playlist: a word wall's Word of Power
SPECIAL = [f"music\\special\\mus_special_wordofpower_{i:02d}.xwm" for i in range(1, 4)] + \
          [f"music\\reward\\mus_reward_{i:02d}.xwm" for i in range(1, 5)] + \
          [f"music\\mus_discover_genericlocation_{i:02d}.xwm" for i in range(1, 4)] + \
          [f"music\\mus_levelup_{i:02d}.xwm" for i in range(1, 4)] + \
          ["music\\combat\\mus_combat_boss.xwm"]  # (a quest done, a place found, a level up; a dragon's fight)

HERE = os.path.dirname(os.path.abspath(__file__))


def load_adpcm():
    """The ADPCM coder (adpcm.c) as a library: the one shipped with the data tool's program, else
    built here with the machine's C compiler the first time (and again when adpcm.c changes)."""
    ext = {"darwin": "dylib", "win32": "dll"}.get(sys.platform, "so")
    shipped = os.path.join(getattr(sys, "_MEIPASS", HERE), "libadpcm." + ext)
    src = os.path.join(HERE, "adpcm.c")
    lib = os.path.join(HERE, "build", "libadpcm." + ext)
    if os.path.exists(shipped):
        lib = shipped
    elif not os.path.exists(lib) or os.path.getmtime(lib) < os.path.getmtime(src):
        os.makedirs(os.path.dirname(lib), exist_ok=True)
        for cc in ("cc", "gcc", "clang"):
            try:
                subprocess.check_call([cc, "-O2", "-shared", "-fPIC", "-o", lib, src])
                break
            except (OSError, subprocess.CalledProcessError):
                continue
        else:
            sys.exit("a C compiler (cc, gcc or clang) is needed once, to build converter/adpcm.c")
    dll = ctypes.CDLL(lib)
    for fn in (dll.encode_channel, dll.decode_channel):
        fn.restype = None
    return dll


def decode_xwm(data):
    """xWMA bytes -> int16 array (frames, 2) at RATE, peak-normalized."""
    container = av.open(io.BytesIO(data))
    stream = container.streams.audio[0]
    resampler = av.AudioResampler(format="flt", layout="stereo", rate=RATE)
    chunks = []
    for frame in container.decode(stream):
        for out in resampler.resample(frame):
            chunks.append(out.to_ndarray().reshape(-1, 2))
    for out in resampler.resample(None):
        chunks.append(out.to_ndarray().reshape(-1, 2))
    pcm = np.concatenate(chunks)
    gain = min(MAX_GAIN, PEAK / max(float(np.abs(pcm).max()), 1e-6))
    return np.clip(pcm * gain * 32767.0, -32768, 32767).astype(np.int16), gain


def encode_skmus(dll, pcm):
    pcm = np.ascontiguousarray(pcm)
    frames, channels = pcm.shape
    blocks = (frames + BLOCK_FRAMES - 1) // BLOCK_FRAMES
    ch_block = 4 + BLOCK_FRAMES // 2
    body = ctypes.create_string_buffer(blocks * ch_block * channels)
    src = pcm.ctypes.data_as(ctypes.POINTER(ctypes.c_int16))
    for ch in range(channels):
        dll.encode_channel(ctypes.byref(src.contents, ch * 2), channels, frames, BLOCK_FRAMES,
                           ctypes.byref(body, ch * ch_block), ch_block * channels)
    # Round trip through the reference decoder: report the coding error
    back = np.zeros_like(pcm)
    dst = back.ctypes.data_as(ctypes.POINTER(ctypes.c_int16))
    for ch in range(channels):
        dll.decode_channel(ctypes.byref(body, ch * ch_block), ch_block * channels, frames, BLOCK_FRAMES,
                           ctypes.byref(dst.contents, ch * 2), channels)
    err = pcm.astype(np.float64) - back
    snr = 10 * np.log10((pcm.astype(np.float64) ** 2).mean() / max((err ** 2).mean(), 1e-9))
    header = struct.pack("<4sHHIII", b"SKMU", SKMU_VERSION, channels, RATE, frames, BLOCK_FRAMES)
    return header + body.raw, snr


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out_dir = os.path.join(sys.argv[1], "music")
    os.makedirs(out_dir, exist_ok=True)
    dll = load_adpcm()
    sounds = BSA(os.path.join(SKYRIM_DATA, "Skyrim - Sounds.bsa"))
    names = []
    for path in TRACKS + SPECIAL:
        name = path.rsplit("\\", 1)[1][:-4]
        if path in SPECIAL and os.path.exists(os.path.join(out_dir, name + ".skmus")) and "--all" not in sys.argv:
            continue
        pcm, gain = decode_xwm(sounds.read(path))
        data, snr = encode_skmus(dll, pcm)
        with open(os.path.join(out_dir, name + ".skmus"), "wb") as f:
            f.write(data)
        if path in TRACKS:
            names.append(name)
        print(f"{name}: {len(pcm) / RATE:6.1f} s, gain {gain:.2f}, {len(data) / 1e6:5.1f} MB, SNR {snr:.1f} dB")
    with open(os.path.join(out_dir, "music.txt"), "w") as f:
        f.write("\n".join(names) + "\n")
    print(f"{len(names)} tracks -> {out_dir}")


if __name__ == "__main__":
    main()
