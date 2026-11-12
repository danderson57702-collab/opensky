"""Reader for Skyrim SE's Havok 2010 binary packfiles (.hkx): skeletons and animations.

A packfile has three sections: class names, types (empty in game files) and data. The
data section is a heap of objects whose pointers are stored as fixups: local fixups
(pointers within the data section: array contents, strings), global fixups (pointers to
other objects) and virtual fixups (where each object starts, and its class name). Skyrim
SE uses the 64-bit layout (8-byte pointers, 16-byte hkArray: pointer, size, capacity|flags).

Objects read here (64-bit field offsets of hk_2010.2.0-r1):
  hkaSkeleton                    name, parentIndices (s16), bones (hkaBone), referencePose
  hkaSplineCompressedAnimation   Havok's usual animation compression (B-spline tracks of
                                 quantized control points, in blocks of frames)
  hkaAnimationBinding            which skeleton bone each animation track drives
"""
import math
import struct

import numpy as np

MAGIC = (0x57E0E057, 0x10C0C010)


class Object:
    def __init__(self, pack, offset, cls):
        self.pack, self.offset, self.cls = pack, offset, cls

    # field readers at an offset within the object
    def i32(self, o):
        return struct.unpack_from("<i", self.pack.data, self.offset + o)[0]

    def f32(self, o):
        return struct.unpack_from("<f", self.pack.data, self.offset + o)[0]

    def array(self, o):
        """(absolute data offset or None, count) of an hkArray field."""
        count = struct.unpack_from("<i", self.pack.data, self.offset + o + 8)[0]
        return self.pack.local.get(self.offset + o), count

    def pointer(self, o):
        """The object a pointer field points to (global fixup), or None."""
        return self.pack.object_at(self.pack.global_.get(self.offset + o))

    def string(self, o):
        dst = self.pack.local.get(self.offset + o)
        if dst is None:
            return ""
        end = self.pack.data.index(b"\0", dst)
        return self.pack.data[dst:end].decode("ascii", "replace")


class Packfile:
    def __init__(self, data):
        m0, m1, _tag, version = struct.unpack_from("<IIii", data)
        if (m0, m1) != MAGIC or version != 8 or data[16] != 8:
            raise ValueError("not a 64-bit Havok 2010 packfile")
        self.data = data
        sections = {}
        for i in range(struct.unpack_from("<i", data, 20)[0]):
            h = 0x40 + i * 0x30
            tag = data[h:h + 19].split(b"\0")[0].decode()
            start, local, glob, virt, exports, imports, end = struct.unpack_from("<7I", data, h + 20)
            sections[tag] = (start, local, glob, virt, exports, imports, end)
        cstart = sections["__classnames__"][0]
        start, local, glob, virt, exports, _imports, end = sections["__data__"]
        self.base = start
        # Local fixups: (src, dst) relative to the section; -1 src ends the list
        self.local = {}
        for o in range(start + local, start + glob, 8):
            src, dst = struct.unpack_from("<iI", data, o)
            if src >= 0:
                self.local[start + src] = start + dst
        # Global fixups: (src, dst section, dst)
        self.global_ = {}
        for o in range(start + glob, start + virt, 12):
            src, _sec, dst = struct.unpack_from("<iiI", data, o)
            if src >= 0:
                self.global_[start + src] = start + dst
        # Virtual fixups: (object, class names section, class name offset)
        self.objects = {}
        virt_end = min(x for x in (exports, _imports, end) if x >= virt)
        for o in range(start + virt, min(start + virt_end, len(data) - 11), 12):
            src, _sec, name = struct.unpack_from("<iiI", data, o)
            if src >= 0:
                end_name = data.index(b"\0", cstart + name)
                self.objects[start + src] = data[cstart + name:end_name].decode()

    def object_at(self, offset):
        cls = self.objects.get(offset) if offset is not None else None
        return Object(self, offset, cls) if cls else None

    def of_class(self, cls):
        return [Object(self, o, c) for o, c in sorted(self.objects.items()) if c == cls]


# --- Skeleton ----------------------------------------------------------------------------

class Skeleton:
    """Bone names, parents and the reference pose (local translation, rotation xyzw,
    scale) of a skeleton.hkx."""

    def __init__(self, data):
        pack = Packfile(data)
        skel = max(pack.of_class("hkaSkeleton"), key=lambda s: s.array(0x20)[1])
        self.name = skel.string(0x10)
        po, pc = skel.array(0x18)
        self.parents = list(struct.unpack_from(f"<{pc}h", data, po)) if pc else []
        bo, bc = skel.array(0x28)
        self.bones = []
        for i in range(bc):
            dst = pack.local.get(bo + i * 16)
            self.bones.append(data[dst:data.index(b"\0", dst)].decode() if dst else f"bone{i}")
        ro, rc = skel.array(0x38)
        pose = np.frombuffer(data, np.float32, rc * 12, ro).reshape(rc, 12) if rc else np.zeros((0, 12), np.float32)
        self.translation, self.rotation, self.scale = pose[:, 0:3], pose[:, 4:8], pose[:, 8:11]


# --- Spline-compressed animation -----------------------------------------------------------

def _align(p, n):
    return (p + n - 1) // n * n


# Rotation quantizations: size in bytes
ROT_POLAR32, ROT_THREECOMP40, ROT_THREECOMP48, ROT_THREECOMP24, ROT_STRAIGHT16, ROT_UNCOMPRESSED = range(6)
ROT_SIZE = {ROT_POLAR32: 4, ROT_THREECOMP40: 5, ROT_THREECOMP48: 6, ROT_THREECOMP24: 3, ROT_STRAIGHT16: 2,
            ROT_UNCOMPRESSED: 16}
ROT_ALIGN = {ROT_POLAR32: 4, ROT_THREECOMP40: 1, ROT_THREECOMP48: 2, ROT_THREECOMP24: 1, ROT_STRAIGHT16: 2,
             ROT_UNCOMPRESSED: 4}


def _quat_threecomp48(b):
    """Three components as 15-bit values (the largest one dropped and rebuilt)."""
    x, y, z = struct.unpack_from("<3H", b)
    mask = (1 << 15) - 1
    frac = 0.7071067811865476 / mask * 2.0  # values span [-1/sqrt2, 1/sqrt2]
    vals = [((v & mask) - (mask >> 1)) * frac / 2.0 * 2.0 for v in (x, y, z)]
    vals = [((v & mask) - mask / 2.0) * (2 * 0.7071067811865476 / mask) for v in (x, y, z)]
    shift = ((y >> 15) << 1) | (x >> 15)
    neg = z >> 15
    w = math.sqrt(max(0.0, 1.0 - sum(v * v for v in vals)))
    if neg:
        w = -w
    q = vals[:]
    q.insert(shift, w)
    return q  # x, y, z, w


def _quat_threecomp40(b):
    """Three 12-bit components in 40 bits, the largest dropped (2-bit index), sign bit."""
    v = int.from_bytes(b[:5], "little")
    mask = (1 << 12) - 1
    comps = [((v >> (12 * i)) & mask) for i in range(3)]
    vals = [(c - mask / 2.0) * (2 * 0.7071067811865476 / mask) for c in comps]
    shift = (v >> 36) & 3
    neg = (v >> 38) & 1
    w = math.sqrt(max(0.0, 1.0 - sum(x * x for x in vals)))
    if neg:
        w = -w
    q = vals[:]
    q.insert(shift, w)
    return q


def _quat_polar32(b):
    v = struct.unpack_from("<I", b)[0]
    r_mask = (1 << 10) - 1
    phi_frac = (math.pi / 4) / 511.0
    r = (((v >> 18) & r_mask) / r_mask)
    r = 1.0 - r * r
    phi_theta = v & 0x3FFFF
    phi = math.floor(math.sqrt(phi_theta))
    theta = 0.0
    if phi > 0:
        theta = (math.pi / 4) * (phi_theta - phi * phi) / phi
        phi = phi_frac * phi
    magnitude = math.sqrt(max(0.0, 1.0 - r * r))
    q = [math.sin(phi) * math.cos(theta) * magnitude, math.sin(phi) * math.sin(theta) * magnitude,
         math.cos(phi) * magnitude, r]
    for i, bit in enumerate((28, 29, 30, 31)):
        if v & (1 << bit):
            q[i] = -q[i]
    return q


def _read_quat(data, p, qtype):
    b = data[p:p + ROT_SIZE[qtype]]
    if qtype == ROT_THREECOMP40:
        return _quat_threecomp40(b)
    if qtype == ROT_THREECOMP48:
        return _quat_threecomp48(b)
    if qtype == ROT_POLAR32:
        return _quat_polar32(b)
    if qtype == ROT_UNCOMPRESSED:
        return list(struct.unpack_from("<4f", b))
    raise ValueError(f"rotation quantization {qtype} not supported")


def _find_span(degree, u, n, knots):
    """Knot span index for parameter u (n = number of control points - 1)."""
    if u >= knots[n + 1]:
        return n
    low = degree
    while low < n and knots[low + 1] <= u:
        low += 1
    return low


def _bspline(u, degree, knots, cps):
    """De Boor evaluation of a B-spline with control points cps (k x dims)."""
    n = len(cps) - 1
    span = _find_span(degree, u, n, knots)
    d = [np.array(cps[j + span - degree], np.float64) for j in range(degree + 1)]
    for r in range(1, degree + 1):
        for j in range(degree, r - 1, -1):
            i = j + span - degree
            den = knots[i + degree - r + 1] - knots[i]
            a = (u - knots[i]) / den if den != 0 else 0.0
            d[j] = (1 - a) * d[j - 1] + a * d[j]
    return d[degree]


class SplineAnimation:
    """Decodes an hkaSplineCompressedAnimation into local track transforms per frame."""

    def __init__(self, obj):
        data = obj.pack.data
        self.duration = obj.f32(0x14)
        self.num_tracks = obj.i32(0x18)
        self.num_float_tracks = obj.i32(0x1C)
        self.num_frames = obj.i32(0x38)
        self.num_blocks = obj.i32(0x3C)
        self.max_frames_per_block = obj.i32(0x40)
        self.mask_and_quant_size = obj.i32(0x44)
        self.block_duration = obj.f32(0x48)
        self.block_inverse_duration = obj.f32(0x4C)
        self.frame_duration = obj.f32(0x50)
        bo, bc = obj.array(0x58)
        self.block_offsets = struct.unpack_from(f"<{bc}I", data, bo) if bc else ()
        do, dc = obj.array(0x98)
        self.data = data[do:do + dc] if dc else b""

    def _vector_track(self, p, mask, quant, default):
        """Reads one position/scale track at p: (callable(t)->xyz, new p)."""
        d = self.data
        static_bits, spline_bits = mask & 0x0F, mask >> 4
        values = list(default)
        if spline_bits:
            num_items, degree = struct.unpack_from("<HB", d, p)
            p += 3
            knots = list(d[p:p + num_items + degree + 2])
            p += num_items + degree + 2
            p = _align(p, 4)
            mins, maxs = [0.0] * 3, [0.0] * 3
            for c in range(3):
                if spline_bits & (1 << c):
                    mins[c], maxs[c] = struct.unpack_from("<2f", d, p)
                    p += 8
                elif static_bits & (1 << c):
                    values[c] = struct.unpack_from("<f", d, p)[0]
                    p += 4
            size = 1 if quant == 0 else 2
            cps = []
            for _ in range(num_items + 1):
                cp = list(values)
                for c in range(3):
                    if spline_bits & (1 << c):
                        raw = d[p] if size == 1 else struct.unpack_from("<H", d, p)[0]
                        p += size
                        cp[c] = mins[c] + (maxs[c] - mins[c]) * raw / (255.0 if size == 1 else 65535.0)
                cps.append(cp)
            p = _align(p, 4)
            return (lambda u: _bspline(u, degree, knots, cps)), p
        for c in range(3):
            if static_bits & (1 << c):
                values[c] = struct.unpack_from("<f", d, p)[0]
                p += 4
        v = np.array(values, np.float64)
        return (lambda u: v), p

    def _rotation_track(self, p, mask, quant):
        d = self.data
        if mask & 0xF0:  # spline
            num_items, degree = struct.unpack_from("<HB", d, p)
            p += 3
            knots = list(d[p:p + num_items + degree + 2])
            p += num_items + degree + 2
            p = _align(p, ROT_ALIGN[quant])
            cps = []
            for _ in range(num_items + 1):
                cps.append(_read_quat(d, p, quant))
                p += ROT_SIZE[quant]
            # Keep consecutive control points in the same hemisphere
            for i in range(1, len(cps)):
                if np.dot(cps[i], cps[i - 1]) < 0:
                    cps[i] = [-x for x in cps[i]]
            p = _align(p, 4)

            def ev(u):
                q = _bspline(u, degree, knots, cps)
                return q / max(np.linalg.norm(q), 1e-9)
            return ev, p
        if mask & 0x0F:  # static
            p = _align(p, ROT_ALIGN[quant])
            q = np.array(_read_quat(d, p, quant), np.float64)
            p += ROT_SIZE[quant]
            p = _align(p, 4)
            return (lambda u: q), p
        ident = np.array([0.0, 0.0, 0.0, 1.0])
        return (lambda u: ident), p

    def decode(self):
        """[frame][track] -> (translation xyz, rotation xyzw, scale xyz) as numpy arrays."""
        frames = [[None] * self.num_tracks for _ in range(self.num_frames)]
        for b in range(self.num_blocks):
            p = self.block_offsets[b]
            masks = []
            for t in range(self.num_tracks):
                masks.append(tuple(self.data[p + t * 4:p + t * 4 + 4]))
            p += self.mask_and_quant_size  # (the masks, padded)
            first = b * (self.max_frames_per_block - 1)
            count = min(self.max_frames_per_block, self.num_frames - first)
            for t, (quant, pos_mask, rot_mask, scale_mask) in enumerate(masks):
                pos, p = self._vector_track(p, pos_mask, quant & 3, (0.0, 0.0, 0.0))
                rot, p = self._rotation_track(p, rot_mask, (quant >> 2) & 0xF)
                scale, p = self._vector_track(p, scale_mask, (quant >> 6) & 3, (1.0, 1.0, 1.0))
                for f in range(count):
                    if frames[first + f][t] is None:
                        u = float(f)  # knots are in frames within the block
                        frames[first + f][t] = (pos(u), rot(u), scale(u))
        return frames


class Animation:
    """An animation file: its decoded frames and the bones its tracks drive."""

    def __init__(self, data):
        pack = Packfile(data)
        anims = pack.of_class("hkaSplineCompressedAnimation")
        if not anims:
            raise ValueError("no spline-compressed animation: " + ", ".join(sorted(set(pack.objects.values()))))
        spline = SplineAnimation(anims[0])
        self.duration = spline.duration
        self.frames = spline.decode()
        binding = pack.of_class("hkaAnimationBinding")
        self.track_to_bone = None
        if binding:
            to, tc = binding[0].array(0x20)
            if tc:
                self.track_to_bone = list(struct.unpack_from(f"<{tc}h", data, to))


# --- Posing -----------------------------------------------------------------------------------

def quat_to_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def pose_world(skeleton, frame, track_to_bone=None):
    """World (model space, game units) rotation and translation of every bone for one
    decoded frame (bones without a track keep the reference pose)."""
    n = len(skeleton.bones)
    local_t = [skeleton.translation[i] for i in range(n)]
    local_q = [skeleton.rotation[i] for i in range(n)]
    if frame is not None:
        for t, (pos, rot, _scale) in enumerate(frame):
            b = track_to_bone[t] if track_to_bone else t
            if 0 <= b < n:
                local_t[b], local_q[b] = pos, rot
    R, T = [None] * n, [None] * n
    for i in range(n):
        p = skeleton.parents[i]
        r = quat_to_matrix(local_q[i])
        if p < 0:
            R[i], T[i] = r, np.array(local_t[i], np.float64)
        else:
            T[i] = T[p] + R[p] @ np.asarray(local_t[i], np.float64)
            R[i] = R[p] @ r
    return R, T


def ground_speed(skeleton, anim, fps=30.0):
    """Walking speed (game units/s) of an in-place locomotion cycle: the planted (lowest)
    foot slides back under the body at the speed the body moves forward. 0 if it doesn't
    travel. The sign is along +Y (Skyrim's forward)."""
    feet = [skeleton.bones.index(n) for n in ("NPC L Foot [Lft ]", "NPC R Foot [Rft ]") if n in skeleton.bones]
    if len(feet) < 2 or len(anim.frames) < 3:
        return 0.0
    pos = []
    for frame in anim.frames:
        _R, T = pose_world(skeleton, frame, anim.track_to_bone)
        pos.append([T[f] for f in feet])
    pos = np.array(pos)  # frames x 2 x 3
    dt = anim.duration / max(len(anim.frames) - 1, 1)
    v = []
    for f in range(len(pos) - 1):
        low = int(np.argmin(pos[f, :, 2]))
        v.append(-(pos[f + 1, low, :2] - pos[f, low, :2]) / dt)  # body velocity = -foot
    v = np.median(np.array(v), axis=0)
    return v  # (x, y) game units per second
