"""Convert Skyrim DDS textures to small DXT1/DXT5 .sktex files for the Vita engine.

.sktex layout (little-endian), version 1:
    char[4] magic = "SKTX"
    u16 version, u16 format (1 = DXT1, 5 = DXT5)
    u16 width, u16 height, u16 mip_count, u16 reserved
    mip_count x { u32 size; u8 data[size] }   largest level first
"""
import io
import struct

import etcpak
import numpy as np
from PIL import Image

SKTX_VERSION = 1
FORMAT_DXT1 = 1
FORMAT_DXT5 = 5
MIN_MIP = 4  # one DXT block


def _encode_dxt(img, fmt):
    # etcpak rather than Pillow's encoder: similar RMSE, but Pillow's BC1 picks
    # saturated outlier endpoints (4x more red speckle on tundra01).
    raw = img.convert("RGBA").tobytes()
    if fmt == FORMAT_DXT1:
        data = etcpak.compress_bc1(raw, img.width, img.height)
    else:
        data = etcpak.compress_bc3(raw, img.width, img.height)
    expected = max(1, img.width // 4) * max(1, img.height // 4) * (8 if fmt == FORMAT_DXT1 else 16)
    if len(data) != expected:
        raise ValueError(f"unexpected DXT payload {len(data)} != {expected}")
    return data


def _apply_gain(img, gain):
    px = np.asarray(img, dtype=np.float32)
    px[..., :3] = np.clip(px[..., :3] * gain, 0, 255)
    return Image.fromarray(px.astype(np.uint8))


ALPHA_TEST_REF = 0.5  # the engine's alpha-test reference (objects.c: u_alphaRef)


def convert(dds_bytes, max_size=128, keep_alpha=False, gain=1.0, alpha_test=False, cut=0):
    """Returns (sktex_bytes, preview_image).

    Skyrim diffuse maps often store gloss in alpha, so alpha is dropped (DXT1)
    unless the caller asks for it, e.g. for alpha-tested foliage. `gain`
    brightens textures authored for Skyrim's HDR lighting (landscape is dark).
    alpha_test: alpha is a cut-out mask; every mip keeps the original's coverage.
    """
    src = Image.open(io.BytesIO(dds_bytes))
    src.load()
    src = src.convert("RGBA")
    if not keep_alpha:
        src.putalpha(255)
    elif cut > 2:  # (Skyrim's own cut - a dragon's remains at 228 of 255: what's under it, gone; the rest solid)
        a = np.asarray(src)[..., 3]
        src.putalpha(Image.fromarray(np.where(a >= cut, 255, 0).astype(np.uint8)))
    if gain != 1.0:
        src = _apply_gain(src, gain)
    coverage = _coverage(np.asarray(src)[..., 3]) if alpha_test else None
    sktex, base = _encode_sktex(src, max_size, keep_alpha, coverage=coverage)
    return sktex, base if keep_alpha else base.convert("RGB")


def _coverage(alpha, scale=1.0):
    """Fraction of texels that pass the alpha test after scaling alpha."""
    return float((np.minimum(alpha.astype(np.float32) * scale, 255.0) >= ALPHA_TEST_REF * 255.0).mean())


def _preserve_coverage(img, target):
    """Scales a mip's alpha so as many texels pass the alpha test as in the original.
    Plain downsampling averages cut-out edges below the threshold, so thin boards,
    thatch and leaves dissolve into holes that shimmer with distance."""
    px = np.asarray(img).copy()
    alpha = px[..., 3]
    lo, hi = 1.0, 8.0 # only ever raise alpha (mips that kept their coverage are untouched)
    for _ in range(20):
        mid = (lo + hi) * 0.5
        if _coverage(alpha, mid) < target:
            lo = mid
        else:
            hi = mid
    px[..., 3] = np.clip(alpha.astype(np.float32) * hi, 0, 255).astype(np.uint8)
    return Image.fromarray(px)


def convert_image(img, max_size=128, keep_alpha=False, mip_fade=None):
    """Encodes an in-memory PIL image (e.g. a baked terrain color map) as .sktex bytes.

    mip_fade: optional per-level weights (level 0 first); each mip is blended toward
    the image's mean color by (1 - weight), levels past the list use the last weight.
    Used for detail textures so they fade out with distance instead of tiling visibly.
    """
    img = img.convert("RGBA")
    if not keep_alpha:
        img.putalpha(255)
    return _encode_sktex(img, max_size, keep_alpha, mip_fade)[0]


def _fade_toward_mean(img, weight):
    px = np.asarray(img, dtype=np.float32)
    mean = px[..., :3].mean(axis=(0, 1))
    px[..., :3] = mean + (px[..., :3] - mean) * weight
    return Image.fromarray(np.clip(px + 0.5, 0, 255).astype(np.uint8))


def _bleed(img, passes=8):
    """Colour spread into the fully transparent texels from their visible neighbours: filtered
    (bilinear, smaller mips) they'd otherwise mix in their black, darkening cut-out edges
    (leaves, hair, fences) into dark fringes that shimmer and pop with distance."""
    px = np.asarray(img, np.float32).copy()
    seen = px[..., 3] > 0
    if seen.all() or not seen.any():
        return img
    rgb, w = px[..., :3] * seen[..., None], seen.astype(np.float32)
    for _ in range(passes):
        sw = w.copy()
        srgb = rgb.copy()
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            sw += np.roll(w, (dy, dx), (0, 1))
            srgb += np.roll(rgb, (dy, dx), (0, 1))
        fill = (w == 0) & (sw > 0)
        rgb[fill] = srgb[fill] / sw[fill][:, None]
        w = np.where(fill, 1.0, w)
    rest = w == 0
    if rest.any():
        rgb[rest] = rgb[~rest].mean(0)
    px[..., :3] = np.where(seen[..., None], px[..., :3], rgb)
    return Image.fromarray(np.clip(px + 0.5, 0, 255).astype(np.uint8))


def _resize(img, size, method):
    """Resized with the colour weighted by alpha (a transparent texel's colour counts for
    nothing), so smaller levels keep cut-out edges their own colour."""
    if img.mode != "RGBA" or np.asarray(img)[..., 3].min() == 255:
        return img.resize(size, method)
    px = np.asarray(img, np.float32) / 255.0
    a = px[..., 3]
    bands = [Image.fromarray(px[..., k] * a) for k in range(3)] + [Image.fromarray(a)]
    out = [np.asarray(b.resize(size, method), np.float32) for b in bands]
    alpha = np.clip(out[3], 0.0, 1.0)
    plain = np.asarray(img.convert("RGB").resize(size, method), np.float32) / 255.0
    rgb = np.where(alpha[..., None] > 1e-3, np.stack(out[:3], -1) / np.maximum(alpha, 1e-3)[..., None], plain)
    res = np.concatenate([np.clip(rgb, 0, 1), alpha[..., None]], -1)
    return Image.fromarray((res * 255 + 0.5).astype(np.uint8))


def _encode_sktex(src, max_size, keep_alpha, mip_fade=None, coverage=None):
    fmt = FORMAT_DXT5 if keep_alpha else FORMAT_DXT1
    scale = min(1.0, max_size / max(src.size))
    w = max(MIN_MIP, int(src.width * scale))
    h = max(MIN_MIP, int(src.height * scale))
    if keep_alpha:
        src = _bleed(src)
    base = _resize(src, (w, h), Image.LANCZOS) if (w, h) != src.size else src

    mips = []
    level = base
    while True:
        if coverage is not None:
            level = _preserve_coverage(level, coverage)
        if mip_fade:
            fade = mip_fade[min(len(mips), len(mip_fade) - 1)]
            mips.append(_encode_dxt(_fade_toward_mean(level, fade) if fade < 1.0 else level, fmt))
        else:
            mips.append(_encode_dxt(level, fmt))
        if level.width <= MIN_MIP or level.height <= MIN_MIP:
            break
        level = _resize(level, (level.width // 2, level.height // 2), Image.BOX)
        if keep_alpha:
            level = _bleed(level, passes=4)

    out = bytearray(struct.pack("<4sHHHHHH", b"SKTX", SKTX_VERSION, fmt, w, h, len(mips), 0))
    for m in mips:
        out += struct.pack("<I", len(m)) + m
    return bytes(out), base
