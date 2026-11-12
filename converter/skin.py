"""Skinned (character) meshes from Skyrim SE NIFs, and posing them on the skeleton.

Character parts - FaceGen heads (head, mouth, eyes, brows, hair, beard), bodies, hands,
feet and armour - are skinned to Skyrim's shared skeleton. Their vertices live in each
part's skin space; a vertex lands in the world at

    v_world = sum_i  w_i * (BoneWorld[bone_i] * SkinToBone[bone_i]) * v

where SkinToBone comes from the part's NiSkinData and BoneWorld from the skeleton,
looked up by bone name. Using one skeleton for every part makes them line up, and
changing BoneWorld (rotating bones) poses the character.
"""
import struct

import numpy as np

from nif import NIF, Reader, VF_NORMAL, VF_SKINNED, VF_UV, VF_VERTEX

SKINNED_SHAPES = {"BSTriShape", "BSDynamicTriShape", "BSSubIndexTriShape"}
SKIN_INSTANCES = {"NiSkinInstance", "BSDismemberSkinInstance"}


def _transform(rot, trans, scale):
    m = np.eye(4)
    m[:3, :3] = np.asarray(rot, dtype=np.float64).reshape(3, 3) * scale
    m[:3, 3] = trans
    return m


class SkinnedShape:
    def __init__(self):
        self.name = ""
        self.positions = None   # (N,3) skin space
        self.uvs = None         # (N,2)
        self.normals = None     # (N,3) skin space, or None
        self.triangles = None   # (M,3)
        self.weights = None     # (N,4)
        self.bones = None       # (N,4) indices into bone_names
        self.bone_names = []
        self.skin_to_bone = []  # 4x4 per bone
        self.texture = None
        self.alpha_test = False
        self.shader = -1        # block index of its lighting shader


def _read_skin_data(nif, idx):
    r = Reader(nif.data, nif.block_offsets[idx])
    rot = r.unpack("9f"); trans = r.unpack("3f"); scale = r.f32()
    overall = _transform(rot, trans, scale)
    nb = r.u32()
    has_weights = r.u8()
    bones = []
    for _ in range(nb):
        rot = r.unpack("9f"); trans = r.unpack("3f"); scale = r.f32()
        r.unpack("4f")  # bounding sphere
        if has_weights:
            nv = r.u16()
            r.p += nv * 6
        bones.append(_transform(rot, trans, scale))
    return overall, bones


def _decode_partition_vertices(nif, offset, count, desc):
    vflags = (desc >> 44) & 0xFFF
    stride = (desc & 0xF) * 4
    uv_off = ((desc >> 8) & 0xF) * 4
    nrm_off = ((desc >> 16) & 0xF) * 4
    skin_off = ((desc >> 28) & 0xF) * 4
    raw = np.frombuffer(nif.data, dtype=np.uint8, count=count * stride, offset=offset).reshape(count, stride)
    pos = None
    if vflags & VF_VERTEX:  # Skyrim SE always stores full-precision positions
        pos = raw[:, 0:12].copy().view(np.float32).reshape(count, 3)
    uvs = (raw[:, uv_off:uv_off + 4].copy().view(np.float16).reshape(count, 2).astype(np.float32)
           if vflags & VF_UV else np.zeros((count, 2), np.float32))
    nrm = raw[:, nrm_off:nrm_off + 3].astype(np.float32) / 127.5 - 1.0 if vflags & VF_NORMAL else None
    if not vflags & VF_SKINNED:
        return None
    weights = raw[:, skin_off:skin_off + 8].copy().view(np.float16).reshape(count, 4).astype(np.float32)
    bones = raw[:, skin_off + 8:skin_off + 12].astype(np.int32)
    return pos, uvs, nrm, weights, bones


def _read_partition(nif, idx):
    """Shared vertex buffer of an SSE NiSkinPartition plus all partitions' triangles."""
    r = Reader(nif.data, nif.block_offsets[idx])
    num_parts, data_size, vertex_size = r.unpack("III")
    desc = r.unpack("Q")[0]
    vertex_offset, vertex_count = r.p, (data_size // vertex_size if vertex_size else 0)
    r.p += data_size
    tris = []
    for _ in range(num_parts):
        nv, nt, nb, ns, nw = r.unpack("5H")
        r.p += 2 * nb
        if r.u8():
            r.p += 2 * nv
        has_weights = r.u8()
        if has_weights:
            r.p += nv * nw * (2 if has_weights == 15 else 4)
        strip_lengths = r.unpack(f"{ns}H")
        if r.u8():
            r.p += 2 * sum(strip_lengths) if ns else 6 * nt
        if r.u8():
            r.p += nv * nw
        r.p += 2 + 8
        tris.append(np.frombuffer(nif.data, dtype=np.uint16, count=nt * 3, offset=r.p).reshape(nt, 3))
        r.p += 6 * nt
    return vertex_offset, vertex_count, desc, (np.concatenate(tris) if tris else np.zeros((0, 3), np.uint16))


def skinned_shapes(nif):
    """Every visible skinned shape of a NIF (character parts)."""
    out = []
    for idx, t in enumerate(nif.block_types):
        if t not in SKINNED_SHAPES:
            continue
        r = Reader(nif.data, nif.block_offsets[idx])
        name, flags, node_t, node_rot, node_s = nif._read_av_object(r)
        if flags & 1:
            continue
        r.unpack("4f")
        skin, shader, alpha = r.i32(), r.i32(), r.i32()
        r.unpack("Q")
        num_tris, num_verts, data_size = r.unpack("HHI")
        if skin < 0 or nif.block_types[skin] not in SKIN_INSTANCES:
            continue
        if data_size:  # inline geometry is unusual for skinned SSE shapes; skip it
            continue
        sr = Reader(nif.data, nif.block_offsets[skin])
        data_idx, part_idx, _root = sr.i32(), sr.i32(), sr.i32()
        bone_nodes = sr.unpack(f"{sr.u32()}i")
        if data_idx < 0 or part_idx < 0:
            continue
        overall, skin_to_bone = _read_skin_data(nif, data_idx)
        voff, vcount, desc, tris = _read_partition(nif, part_idx)
        dec = _decode_partition_vertices(nif, voff, vcount, desc)
        if dec is None or not len(tris):
            continue
        pos, uvs, nrm, weights, bones = dec
        if t == "BSDynamicTriShape":
            # Positions follow the (empty) inline data: particle data size, then the
            # dynamic vertex array (Vector4 per vertex)
            r.p += 0  # no inline vertex or triangle data (data_size == 0)
            particle = r.u32()
            if particle:
                continue
            dyn_size = r.u32()
            dyn = np.frombuffer(nif.data, dtype=np.float32, count=dyn_size // 4, offset=r.p).reshape(-1, 4)
            if len(dyn) != vcount:
                continue
            pos = dyn[:, :3].copy()
        if pos is None:
            continue
        # Positions are relative to the shape's own node (FaceGen heads carry one), while
        # the skin data expects model space
        node = _transform(node_rot, node_t, node_s)
        if not np.allclose(node, np.eye(4)):
            pos = (np.einsum("ij,nj->ni", node[:3, :3], pos.astype(np.float64)) + node[:3, 3]).astype(np.float32)
            if nrm is not None:
                nrm = np.einsum("ij,nj->ni", node[:3, :3], nrm.astype(np.float64)).astype(np.float32)
        s = SkinnedShape()
        s.name = name
        s.positions, s.uvs, s.normals, s.triangles = pos, uvs, nrm, tris.astype(np.int32)
        wsum = weights.sum(1, keepdims=True)
        s.weights = weights / np.where(wsum > 0, wsum, 1)
        s.bones = np.clip(bones, 0, len(bone_nodes) - 1)
        s.bone_names = [nif._read_object_net(Reader(nif.data, nif.block_offsets[b])) for b in bone_nodes]
        s.skin_to_bone = [m @ overall for m in skin_to_bone]
        s.texture = nif._shader_diffuse(shader)
        s.shader = shader
        s.alpha_test = nif.alpha_cutout(alpha)  # (blended brows and face paint too)
        out.append(s)
    return out


class Skeleton:
    """Node hierarchy of a skeleton NIF: bind-pose local transforms and parents by name."""

    def __init__(self, nif):
        self.local, self.parent = {}, {}
        self._walk(nif, 0, None)

    def _walk(self, nif, idx, parent):
        if idx < 0 or nif.block_types[idx] not in ("NiNode", "BSFadeNode"):
            return
        name, _flags, t, rot, scale, children = nif._read_node(idx)
        self.local[name] = _transform(rot, t, scale)
        self.parent[name] = parent
        for c in children:
            self._walk(nif, c, name)

    def world(self, local_overrides=None):
        """World matrix of every bone; local_overrides {name: extra 4x4 applied in the
        bone's local frame} poses the skeleton."""
        local_overrides = local_overrides or {}
        out = {}

        def get(name):
            if name in out:
                return out[name]
            m = self.local[name]
            if name in local_overrides:
                m = m @ local_overrides[name]
            p = self.parent.get(name)
            out[name] = (get(p) @ m) if p else m
            return out[name]

        for n in self.local:
            get(n)
        return out


def pose_shape(shape, bone_world):
    """Skinned positions/normals in skeleton space for the given bone world matrices.
    Bones missing from the skeleton keep their bind placement (identity)."""
    mats = np.stack([bone_world.get(n, np.eye(4)) @ s2b for n, s2b in zip(shape.bone_names, shape.skin_to_bone)])
    hp = np.concatenate([shape.positions.astype(np.float64), np.ones((len(shape.positions), 1))], axis=1)
    pos = np.zeros((len(hp), 3))
    nrm = np.zeros((len(hp), 3)) if shape.normals is not None else None
    for k in range(4):
        m = mats[shape.bones[:, k]]                  # (N,4,4)
        w = shape.weights[:, k:k + 1]
        pos += w * np.einsum("nij,nj->ni", m, hp)[:, :3]
        if nrm is not None:
            nrm += w * np.einsum("nij,nj->ni", m[:, :3, :3], shape.normals.astype(np.float64))
    if nrm is not None:
        nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-6)
    return pos.astype(np.float32), (nrm.astype(np.float32) if nrm is not None else None)


def load_nif(archives, path, find):
    return NIF(find(archives, path))
