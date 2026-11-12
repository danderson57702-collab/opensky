"""Minimal reader for Skyrim SE NIF files (20.2.0.7, user version 12, BS version 100).

Only what static world geometry needs: the NiNode hierarchy with transforms,
BSTriShape geometry (positions, UVs, normals, vertex colors, triangles) and the
diffuse texture path plus alpha-test flag of each shape. Every other block is
skipped using the per-block sizes stored in the header.
"""
import struct

import numpy as np

NODE_TYPES = {"NiNode", "BSFadeNode", "BSLeafAnimNode", "BSMultiBoundNode", "BSOrderedNode",
              "BSValueNode", "BSTreeNode", "NiSwitchNode", "NiBillboardNode", "BSBlastNode",
              "BSDamageStage", "BSMasterParticleSystem", "NiLODNode"}
SHAPE_TYPES = {"BSTriShape", "BSLODTriShape", "BSMeshLODTriShape", "BSDynamicTriShape"}

# BSVertexDesc attribute flags (bits 44..55 of the descriptor)
VF_VERTEX, VF_UV, VF_NORMAL, VF_TANGENT, VF_COLORS, VF_SKINNED = 0x1, 0x2, 0x8, 0x10, 0x20, 0x40

ALPHA_TEST_FLAG = 0x200
ALPHA_BLEND_FLAG = 0x001
SLSF1_REFRACTION = 0x8000  # heat haze / distortion meshes; drawn with a normal map we can't use
SLSF1_VERTEX_ALPHA = 0x8      # vertex alpha is opacity...
SLSF2_VERTEX_COLORS = 0x20    # vertex rgb tints the surface
SLSF2_TREE_ANIM = 1 << 29     # ...except here: trees/plants keep their wind-sway weight in it
SLSF2_DOUBLE_SIDED = 0x10
SLSF1_DECALS = (1 << 26) | (1 << 27)  # decal, dynamic decal: drawn over the surface it lies on
WATER_TEXTURE = "_water"  # (extract_objects.WATER_TEXTURE)
SHADER_FACEGEN, SHADER_HAIR_TINT = 4, 6  # BSLightingShaderProperty types     # drawn from both sides (foliage, banners); everything else culls


class Reader:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def unpack(self, fmt):
        v = struct.unpack_from("<" + fmt, self.d, self.p)
        self.p += struct.calcsize("<" + fmt)
        return v

    def u8(self): return self.unpack("B")[0]
    def u16(self): return self.unpack("H")[0]
    def u32(self): return self.unpack("I")[0]
    def i32(self): return self.unpack("i")[0]
    def f32(self): return self.unpack("f")[0]

    def sized_string(self):
        n = self.u32()
        s = self.d[self.p:self.p + n].decode("cp1252")
        self.p += n
        return s


class Shape:
    def __init__(self, name, positions, uvs, normals, colors, triangles, texture, alpha_test,
                 effect_texture=None, double_sided=None, decal=False):
        self.name = name
        self.positions = positions    # (N,3) float32, in NIF root space
        self.uvs = uvs                # (N,2) float32
        self.normals = normals        # (N,3) float32 or None
        self.colors = colors          # (N,4) uint8 or None
        self.triangles = triangles    # (M,3) uint16
        self.texture = texture        # diffuse path, normalized "textures\\..." or None
        self.alpha_test = alpha_test
        self.effect_texture = effect_texture  # source texture of a BSEffectShaderProperty (fire, glow...)
        # Unknown (no lighting shader): alpha-tested cards are assumed to be foliage
        self.double_sided = alpha_test if double_sided is None else double_sided
        self.decal = decal  # lies on another surface (moss, plaster, wall caps): Skyrim offsets its depth


class NIF:
    def __init__(self, data):
        self.data = data
        self._read_header()

    def _read_header(self):
        d = self.data
        nl = d.index(b"\n")
        if not d[:nl].startswith(b"Gamebryo File Format, Version 20.2.0.7"):
            raise ValueError("not a 20.2.0.7 NIF")
        r = Reader(d, nl + 1)
        version, _endian, user_version, num_blocks, self.bs_version = r.unpack("IBIII")
        if version != 0x14020007 or user_version != 12 or self.bs_version != 100:
            raise ValueError(f"unsupported NIF: uv={user_version} bsver={self.bs_version} (need SSE, 100)")
        for _ in range(3):  # author, process script, export script
            r.p += 1 + r.d[r.p]
        types = [r.sized_string() for _ in range(r.u16())]
        type_idx = r.unpack(f"{num_blocks}H")
        sizes = r.unpack(f"{num_blocks}I")
        num_strings, _max_len = r.unpack("II")
        self.strings = [r.sized_string() for _ in range(num_strings)]
        num_groups = r.u32()
        r.p += 4 * num_groups

        self.block_types = [types[i & 0x7FFF] for i in type_idx]
        self.block_offsets = []
        pos = r.p
        for s in sizes:
            self.block_offsets.append(pos)
            pos += s

    def string(self, idx):
        return self.strings[idx] if 0 <= idx < len(self.strings) else ""

    # --- block readers -------------------------------------------------------

    def _read_object_net(self, r):
        name = self.string(r.i32())
        num_extra = r.u32()
        r.p += 4 * num_extra  # extra data refs
        r.i32()             # controller
        return name

    def _read_av_object(self, r):
        name = self._read_object_net(r)
        flags = r.u32()
        t = np.array(r.unpack("3f"), dtype=np.float64)
        rot = np.array(r.unpack("9f"), dtype=np.float64).reshape(3, 3)
        scale = r.f32()
        self._last_collision = r.i32()  # collision object, read by the hierarchy walk
        return name, flags, t, rot, scale

    def _read_node(self, idx):
        r = Reader(self.data, self.block_offsets[idx])
        name, flags, t, rot, scale = self._read_av_object(r)
        children = list(r.unpack(f"{r.u32()}i"))
        return name, flags, t, rot, scale, children

    def _switch_index(self, idx, num_children):
        r = Reader(self.data, self.block_offsets[idx])
        self._read_av_object(r)
        num_children_field = r.u32()
        r.p += 4 * num_children_field
        num_effects = r.u32()
        r.p += 4 * num_effects
        r.u16()                  # switch flags
        index = r.u32()
        return index if index < num_children else 0

    def _texture_set(self, idx):
        r = Reader(self.data, self.block_offsets[idx])
        return [r.sized_string() for _ in range(r.u32())]

    def lighting_shader(self, idx):
        """(shader type, texture paths, hair tint rgb or None) of a BSLightingShaderProperty."""
        if idx < 0 or self.block_types[idx] != "BSLightingShaderProperty":
            return None
        r = Reader(self.data, self.block_offsets[idx])
        shader_type = r.u32()
        self._read_object_net(r)
        r.unpack("II")               # shader flags
        r.unpack("4f")               # uv offset, uv scale
        tex_set = r.i32()
        textures = [_normalize_texture_path(t) if t else "" for t in self._texture_set(tex_set)] if tex_set >= 0 else []
        tint = None
        if shader_type == SHADER_HAIR_TINT:  # the tint colour closes the block
            end = self.block_offsets[idx + 1] if idx + 1 < len(self.block_offsets) else len(self.data)
            tint = struct.unpack_from("<3f", self.data, end - 12)
        return shader_type, textures, tint

    def _shader_diffuse(self, idx):
        if idx < 0 or self.block_types[idx] != "BSLightingShaderProperty":
            return None
        r = Reader(self.data, self.block_offsets[idx])
        r.u32()                      # skyrim shader type
        self._read_object_net(r)
        flags1, _flags2 = r.unpack("II")
        if flags1 & SLSF1_REFRACTION:
            return None
        r.unpack("4f")               # uv offset, uv scale
        tex_set = r.i32()
        if tex_set < 0:
            return None
        textures = self._texture_set(tex_set)
        return _normalize_texture_path(textures[0]) if textures and textures[0] else None

    def _shader_flags(self, idx):
        """(flags1, flags2) of a BSLightingShaderProperty, or None."""
        if idx < 0 or self.block_types[idx] != "BSLightingShaderProperty":
            return None
        r = Reader(self.data, self.block_offsets[idx])
        r.u32()                      # skyrim shader type
        self._read_object_net(r)
        return r.unpack("II")

    def _effect_source(self, idx):
        if idx < 0 or self.block_types[idx] != "BSEffectShaderProperty":
            return None
        r = Reader(self.data, self.block_offsets[idx])
        self._read_object_net(r)
        r.unpack("II")               # shader flags 1/2
        r.unpack("4f")               # uv offset, uv scale
        src = r.sized_string()
        return _normalize_texture_path(src) if src else None

    def alpha_cutout(self, idx):
        """0 if solid, else non-zero: how a shape is see-through in places (alpha tested, or alpha blended: a
        character's brows, face paint and lashes blend) - drawn alpha-tested with a soft edge."""
        if idx < 0 or self.block_types[idx] != "NiAlphaProperty":
            return False
        r = Reader(self.data, self.block_offsets[idx])
        self._read_object_net(r)
        flags = r.u16()
        if not flags & (ALPHA_TEST_FLAG | ALPHA_BLEND_FLAG):
            return 0
        if not flags & ALPHA_TEST_FLAG:
            return 1  # (blended only: the engine's default cut and a soft edge)
        # Alpha tested: its own threshold (a mammoth's fur cuts at 10 of 255, not half), as
        # the cut's byte; 1 and 2 mean the default cut and glow, so it starts at 3
        return max(3, r.u8())

    def _alpha_test(self, idx):
        if idx < 0 or self.block_types[idx] != "NiAlphaProperty":
            return False
        r = Reader(self.data, self.block_offsets[idx])
        self._read_object_net(r)
        return bool(r.u16() & ALPHA_TEST_FLAG)

    def _decode_vertices(self, offset, count, desc):
        """Decodes SSE packed vertex data (BSVertexDataSSE) -> positions, uvs, normals, colors."""
        vflags = (desc >> 44) & 0xFFF
        stride = (desc & 0xF) * 4
        uv_off = ((desc >> 8) & 0xF) * 4
        nrm_off = ((desc >> 16) & 0xF) * 4
        col_off = ((desc >> 24) & 0xF) * 4
        raw = np.frombuffer(self.data, dtype=np.uint8, count=count * stride, offset=offset).reshape(count, stride)
        # SSE stores full-precision float positions at offset 0
        positions = raw[:, 0:12].copy().view(np.float32).reshape(count, 3)
        uvs = (raw[:, uv_off:uv_off + 4].copy().view(np.float16).reshape(count, 2).astype(np.float32)
               if vflags & VF_UV else np.zeros((count, 2), np.float32))
        normals = raw[:, nrm_off:nrm_off + 3].astype(np.float32) / 127.5 - 1.0 if vflags & VF_NORMAL else None
        colors = raw[:, col_off:col_off + 4].copy() if vflags & VF_COLORS else None
        return positions, uvs, normals, colors

    def _skin_partition_geometry(self, skin_idx):
        """Skinned SSE shapes keep their geometry in NiSkinPartition, not inline.

        Returns (vertex_offset, vertex_count, desc, triangles) using the partitions'
        'triangles copy' lists, which index the shared vertex buffer directly. The rest
        pose is used as-is (trees only use their bones for wind sway).
        """
        if skin_idx < 0 or self.block_types[skin_idx] != "NiSkinInstance":
            return None
        r = Reader(self.data, self.block_offsets[skin_idx])
        r.i32()                  # skin data
        part_idx = r.i32()
        if part_idx < 0 or self.block_types[part_idx] != "NiSkinPartition":
            return None
        r = Reader(self.data, self.block_offsets[part_idx])
        num_parts, data_size, vertex_size = r.unpack("III")
        desc = r.unpack("Q")[0]
        if data_size == 0 or vertex_size == 0:
            return None
        vertex_offset, vertex_count = r.p, data_size // vertex_size
        r.p += data_size
        tris = []
        for _ in range(num_parts):
            nv, nt, nb, ns, nw = r.unpack("5H")
            r.p += 2 * nb                                    # bones
            if r.u8():
                r.p += 2 * nv                                # vertex map
            has_weights = r.u8()
            if has_weights:
                r.p += nv * nw * (2 if has_weights == 15 else 4)
            strip_lengths = r.unpack(f"{ns}H")
            if r.u8():                                       # has faces
                r.p += 2 * sum(strip_lengths) if ns else 6 * nt
            if r.u8():
                r.p += nv * nw                               # bone indices
            r.p += 2 + 8                                     # LOD level, global VB, vertex desc
            tris.append(np.frombuffer(self.data, dtype=np.uint16, count=nt * 3, offset=r.p).reshape(nt, 3))
            r.p += 6 * nt
        return vertex_offset, vertex_count, desc, np.concatenate(tris).copy() if tris else None

    def _read_shape(self, idx):
        r = Reader(self.data, self.block_offsets[idx])
        name, flags, t, rot, scale = self._read_av_object(r)
        r.unpack("4f")                       # bounding sphere
        skin = r.i32()
        shader, alpha = r.i32(), r.i32()
        desc = r.unpack("Q")[0]
        num_tris, num_verts, data_size = r.unpack("HHI")

        if data_size > 0 and num_verts > 0:
            vertex_offset, vertex_count = r.p, num_verts
            tris = np.frombuffer(self.data, dtype=np.uint16, count=num_tris * 3,
                                 offset=r.p + num_verts * (desc & 0xF) * 4).reshape(num_tris, 3).copy()
        else:
            geom = self._skin_partition_geometry(skin)
            if geom is None or geom[3] is None:
                return None
            vertex_offset, vertex_count, desc, tris = geom
        if not (desc >> 44) & VF_VERTEX:
            return None

        positions, uvs, normals, colors = self._decode_vertices(vertex_offset, vertex_count, desc)
        shader_flags = self._shader_flags(shader)
        if colors is not None and shader_flags is not None:
            # Only keep what the material actually uses the vertex colours for
            flags1, flags2 = shader_flags
            if not flags2 & SLSF2_VERTEX_COLORS:
                colors[:, :3] = 255
            if not flags1 & SLSF1_VERTEX_ALPHA or flags2 & SLSF2_TREE_ANIM:
                colors[:, 3] = 255
        double_sided = bool(shader_flags[1] & SLSF2_DOUBLE_SIDED) if shader_flags is not None else None
        decal = bool(shader_flags[0] & SLSF1_DECALS) if shader_flags is not None else False
        # Water surfaces placed with a model (a city's streams and pools) use Skyrim's water
        # shader: drawn as the engine's water
        water = 0 <= shader < len(self.block_types) and self.block_types[shader] == "BSWaterShaderProperty"
        return Shape(name, positions, uvs, normals, colors, tris,
                     WATER_TEXTURE if water else self._shader_diffuse(shader), False if water else self._alpha_test(alpha),
                     self._effect_source(shader), double_sided, decal), (flags, t, rot, scale)

    # --- animation (doors, chests: Skyrim's Open / Close sequences) ------------

    def inventory_marker(self):
        """Skyrim's inventory view of the model (BSInvMarker "INV"): (rotation about X, Y, Z
        in radians, zoom), or None. The inventory turns the model by these before showing it."""
        for i, t in enumerate(self.block_types):
            if t == "BSInvMarker":
                r = Reader(self.data, self.block_offsets[i])
                r.i32()  # name
                rx, ry, rz = r.unpack("3H")
                return rx / 1000.0, ry / 1000.0, rz / 1000.0, r.f32()
        return None

    def sequence_end_pose(self, name):
        """{node name: (rotation 3x3 or None, translation or None)}: where each node a
        NiControllerSequence (e.g. "Open") animates ends up (its last keys, parent space,
        Skyrim units). Empty if there's no such sequence."""
        out = {}
        for i, t in enumerate(self.block_types):
            if t != "NiControllerSequence":
                continue
            r = Reader(self.data, self.block_offsets[i])
            if self.string(r.i32()) != name:
                continue
            count, _grow = r.u32(), r.u32()
            for _ in range(count):
                interp, _ctrl, _priority, node, _prop, _ctype, _cid, _iid = r.unpack("iiB5i")
                if interp < 0 or self.block_types[interp] != "NiTransformInterpolator":
                    continue
                pose = self._transform_interpolator_end(interp)
                if pose is not None:
                    out[self.string(node)] = pose
        return out

    def lone_transform_end(self):
        """sequence_end_pose for a model animated by its behaviour alone (no sequence, as Skyrim's upright
        sarcophagus lid: Sarcophagus_TopVert01.nif's "Forward"): its one interpolator's last keys, for its
        one node. Empty unless there's exactly one of each."""
        interps = [i for i, t in enumerate(self.block_types) if t == "NiTransformInterpolator"]
        nodes = [i for i, t in enumerate(self.block_types) if t == "NiNode"]
        if len(interps) != 1 or len(nodes) != 1:
            return {}
        pose = self._transform_interpolator_end(interps[0])
        if pose is None:
            return {}
        return {self.string(Reader(self.data, self.block_offsets[nodes[0]]).i32()): pose}

    def _transform_interpolator_end(self, idx):
        r = Reader(self.data, self.block_offsets[idx])
        tx, ty, tz, qw, qx, qy, qz, _scale = r.unpack("8f")
        data = r.i32()
        rot = trans = None
        if abs(qw) < 1e30 and abs(qx) < 1e30:  # (FLT_MAX: not set)
            rot = _quat_matrix((qx, qy, qz, qw))
        if abs(tx) < 1e30:
            trans = np.array([tx, ty, tz])
        if data < 0 or self.block_types[data] != "NiTransformData":
            return rot, trans
        r = Reader(self.data, self.block_offsets[data])
        n = r.u32()
        if n:
            kind = r.u32()
            if kind != 4:  # quaternion keys (linear, quadratic or TBC)
                last = None
                for _ in range(n):
                    _time, w, x, y, z = r.unpack("5f")
                    if kind == 3:
                        r.p += 12
                    last = (x, y, z, w)
                rot = _quat_matrix(last)
            else:  # XYZ: three float key groups (Euler angles, radians)
                angles = []
                for _axis in range(3):
                    k = r.u32()
                    value = 0.0
                    if k:
                        ik = r.u32()
                        for _ in range(k):
                            _time, value = r.unpack("2f")
                            r.p += 8 if ik == 2 else 12 if ik == 3 else 0
                    angles.append(value)
                ax, ay, az = angles
                cx, sx, cy, sy, cz, sz = np.cos(ax), np.sin(ax), np.cos(ay), np.sin(ay), np.cos(az), np.sin(az)
                rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
                ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
                rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
                rot = rz @ ry @ rx
        n = r.u32()
        if n:
            ik = r.u32()
            for _ in range(n):
                _time, x, y, z = r.unpack("4f")
                r.p += 24 if ik == 2 else 12 if ik == 3 else 0
                trans = np.array([x, y, z])
        return rot, trans

    def shapes_by_node(self, node_names):
        """shapes(), and for each shape the first of node_names above it (or None)."""
        self._owner_names = set(node_names)
        self._owners = []
        try:
            out = self.shapes()
        finally:
            owners, self._owner_names = self._owners, None
        return out, owners

    def node_world(self, node_name):
        """(rotation, translation, scale) of a named node in root space, or None."""
        found = []

        def walk(idx, prot, ptrans, pscale, is_root):
            if idx < 0 or self.block_types[idx] not in NODE_TYPES or found:
                return
            name, _flags, t, rot, scale, children = self._read_node(idx)
            if is_root:
                rot, t, scale = np.eye(3), np.zeros(3), 1.0
            w = _compose(prot, ptrans, pscale, rot, t, scale)
            if name == node_name:
                found.append(w)
                return
            for c in children:
                walk(c, *w, False)
        walk(0, np.eye(3), np.zeros(3), 1.0, True)
        return found[0] if found else None

    def node_parent_world(self, node_name):
        """The root-space (rotation, translation, scale) of a named node's parent."""
        found = []

        def walk(idx, prot, ptrans, pscale, is_root):
            if idx < 0 or self.block_types[idx] not in NODE_TYPES or found:
                return
            name, _flags, t, rot, scale, children = self._read_node(idx)
            if name == node_name and not is_root:
                found.append((prot, ptrans, pscale))
                return
            if is_root:
                rot, t, scale = np.eye(3), np.zeros(3), 1.0
            w = _compose(prot, ptrans, pscale, rot, t, scale)
            for c in children:
                walk(c, *w, False)
        walk(0, np.eye(3), np.zeros(3), 1.0, True)
        return found[0] if found else None

    # --- hierarchy -----------------------------------------------------------

    def shapes(self):
        """All visible static shapes, with positions/normals in root space."""
        out = []
        self.collisions = []  # (collision object index, rot, trans, scale) per node
        self._walk(0, np.eye(3), np.zeros(3), 1.0, out, is_root=True)
        return out

    def collision_triangles(self, owner=..., only_layers=None):
        """Skyrim's own (Havok) collision geometry as (N,3,3) triangles in root space,
        Skyrim units. Empty if the model has none (plants, small clutter). owner: only
        that moving part's (see shapes_by_node; None: the parts that don't move); only_layers:
        only bodies on these Havok layers."""
        if not hasattr(self, "collisions"):
            self.shapes()
        tris = []
        for obj, rot, trans, scale, own in self.collisions:
            if owner is not ... and own != owner:
                continue
            body = self._collision_body(obj)
            if body is None:
                continue
            shape_idx, body_rot, body_trans = body
            if self._collision_layer(obj) in NON_BLOCKING_LAYERS:
                continue  # (hanging moss, triggers, water: Skyrim lets you through)
            if only_layers is not None and self._collision_layer(obj) not in only_layers:
                continue
            t = havok_triangles(self, shape_idx)
            if t is None or not len(t):
                continue
            pts = t.reshape(-1, 3)
            if body_rot is not None:
                pts = np.einsum("ij,nj->ni", body_rot, pts) + body_trans
            pts = pts * HAVOK_SCALE
            pts = np.einsum("ij,nj->ni", rot, pts) * scale + trans
            tris.append(pts.reshape(-1, 3, 3))
        return np.concatenate(tris) if tris else np.zeros((0, 3, 3))

    def _collision_layer(self, idx):
        """The Havok layer of a collision object's rigid body (Skyrim's OL_*), or -1."""
        _target, _flags, body = struct.unpack_from("<iHi", self.data, self.block_offsets[idx])
        if body < 0 or not self.block_types[body].startswith("bhkRigidBody"):
            return -1
        return self.data[self.block_offsets[body] + 4]

    def _collision_body(self, idx):
        """bhkCollisionObject -> (shape index, rotation or None, translation)."""
        if idx < 0 or self.block_types[idx] != "bhkCollisionObject":
            return None
        _target, _flags, body = struct.unpack_from("<iHi", self.data, self.block_offsets[idx])
        if body < 0 or not self.block_types[body].startswith("bhkRigidBody"):
            return None
        o = self.block_offsets[body]
        shape = struct.unpack_from("<i", self.data, o)[0]
        if self.block_types[body] != "bhkRigidBodyT":
            return shape, None, None
        # bhkRigidBodyCInfo2010: translation (Vec4) at +52, rotation quaternion (xyzw) at +68
        tx, ty, tz, _ = struct.unpack_from("<4f", self.data, o + 52)
        q = struct.unpack_from("<4f", self.data, o + 68)
        return shape, _quat_matrix(q), np.array([tx, ty, tz])

    def _walk(self, idx, prot, ptrans, pscale, out, is_root=False, owner=None):
        if idx < 0:
            return
        btype = self.block_types[idx]
        if btype in NODE_TYPES:
            name, flags, t, rot, scale, children = self._read_node(idx)
            if owner is None and getattr(self, "_owner_names", None) and name in self._owner_names:
                owner = name
            if flags & 1:  # hidden
                return
            collision = self._last_collision
            if is_root:  # the root's own transform places it in the cell, not the model
                rot, t, scale = np.eye(3), np.zeros(3), 1.0
            wrot, wtrans, wscale = _compose(prot, ptrans, pscale, rot, t, scale)
            if collision >= 0:
                self.collisions.append((collision, wrot, wtrans, wscale, owner))
            if btype == "NiSwitchNode":  # only the active variant is visible
                active = self._switch_index(idx, len(children))
                children = children[active:active + 1]
            for c in children:
                self._walk(c, wrot, wtrans, wscale, out, owner=owner)
        elif btype in SHAPE_TYPES:
            res = self._read_shape(idx)
            if res is None:
                return
            shape, (flags, t, rot, scale) = res
            if flags & 1 or shape.name.lower().startswith("editormarker"):
                return
            wrot, wtrans, wscale = _compose(prot, ptrans, pscale, rot, t, scale)
            # einsum instead of @: numpy 2.0's Accelerate matmul emits spurious FP warnings on macOS
            pos = np.einsum("ij,nj->ni", wrot, shape.positions.astype(np.float64)) * wscale + wtrans
            shape.positions = pos.astype(np.float32)
            if shape.normals is not None:
                n = np.einsum("ij,nj->ni", wrot, shape.normals.astype(np.float64))
                n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-6)
                shape.normals = n.astype(np.float32)
            out.append(shape)
            if getattr(self, "_owner_names", None) is not None:
                self._owners.append(owner)


def _compose(prot, ptrans, pscale, rot, t, scale):
    # Child-to-parent: v' = R v * s + t (NiMatrix3 is row-major, applied to column vectors)
    return prot @ rot, prot @ t * pscale + ptrans, pscale * scale


def _normalize_texture_path(p):
    p = p.replace("/", "\\").lower().lstrip("\\")
    # Some NIFs carry absolute developer paths (e.g. skyrimhd\\build\\pc\\data\\textures\\...)
    i = p.find("textures\\")
    if i >= 0:
        return p[i:]
    if p.startswith("data\\"):
        p = p[5:]
    return "textures\\" + p


# --- Havok collision shapes -----------------------------------------------------------

HAVOK_SCALE = 69.99124  # Skyrim units per Havok unit


# Skyrim's collision layers that don't stop a walker: water, triggers, non-collidable (hanging
# moss, cobwebs), cloud traps, acoustic spaces, actor and projectile zones, gas traps, avoid
# boxes, door detection and the picking / line-of-sight layers
NON_BLOCKING_LAYERS = {11, 12, 15, 16, 21, 22, 23, 24, 34, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46}


def _quat_matrix(q):
    x, y, z, w = q
    n = np.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def _box(center, half, rot=None):
    c = np.array([[sx, sy, sz] for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)], float) * half
    if rot is not None:
        c = c @ rot.T
    c = c + center
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return np.array([[c[a], c[b], c[d]] for a, b, cc, d in faces] + [[c[b], c[cc], c[d]] for a, b, cc, d in faces])


def _capsule(p1, p2, r, sides=8):
    """Prism along p1-p2 with pointed caps: a cheap stand-in for a capsule."""
    axis = p2 - p1
    length = np.linalg.norm(axis)
    if length < 1e-6:
        return _box(p1, np.array([r, r, r]))
    a = axis / length
    u = np.cross(a, [0, 0, 1] if abs(a[2]) < 0.9 else [1, 0, 0])
    u /= np.linalg.norm(u)
    v = np.cross(a, u)
    ring = [r * (np.cos(t) * u + np.sin(t) * v) for t in np.linspace(0, 2 * np.pi, sides, endpoint=False)]
    tris = []
    for i in range(sides):
        r0, r1 = ring[i], ring[(i + 1) % sides]
        tris += [[p1 + r0, p1 + r1, p2 + r1], [p1 + r0, p2 + r1, p2 + r0]]
        tris += [[p1 - a * r, p1 + r1, p1 + r0], [p2 + a * r, p2 + r0, p2 + r1]]
    return np.array(tris)


def _convex_from_planes(verts, planes):
    """Triangulates a convex hull given its vertices and face planes (n . x + d = 0)."""
    tris = []
    for p in planes:
        n = p[:3]
        if np.linalg.norm(n) < 1e-6:
            continue
        # The face's vertices are the hull's furthest along the plane normal (Havok's plane
        # offsets include the shape's padding, so testing n . x + d = 0 can miss them all)
        s = verts @ n
        on = verts[s > s.max() - 1e-3 * max(1.0, np.abs(verts).max())]
        if len(on) < 3:
            continue
        c = on.mean(0)
        u = on[0] - c
        if np.linalg.norm(u) < 1e-9:
            continue
        u /= np.linalg.norm(u)
        v = np.cross(n, u)
        order = np.argsort(np.arctan2((on - c) @ v, (on - c) @ u))
        ring = on[order]
        tris += [[c, ring[i], ring[(i + 1) % len(ring)]] for i in range(len(ring))]
    return np.array(tris) if tris else np.zeros((0, 3, 3))


def _compressed_mesh(nif, idx):
    """bhkCompressedMeshShapeData -> (N,3,3) triangles in Havok units."""
    r = Reader(nif.data, nif.block_offsets[idx])
    _bpi, _bpwi, _mwi, _mi, error = r.unpack("4If")
    r.unpack("8f")                     # bounds
    r.unpack("BB")                     # welding type, material type
    for _ in range(3):                 # 32/16/8-bit material tables
        n = r.u32()
        r.p += 4 * n
    count = r.u32()
    r.p += 8 * count                 # chunk materials
    r.u32()                            # named materials (count only)
    transforms = []
    for _ in range(r.u32()):
        t = r.unpack("4f")
        q = r.unpack("4f")
        transforms.append((np.array(t[:3]), _quat_matrix(q)))
    big_verts = np.array([r.unpack("4f")[:3] for _ in range(r.u32())]).reshape(-1, 3)
    tris = []
    for _ in range(r.u32()):           # big triangles: 3 x u16, material u32, welding u16
        a, b, c, _m, _w = r.unpack("3HIH")
        tris.append([big_verts[a], big_verts[b], big_verts[c]])
    for _ in range(r.u32()):           # chunks
        trans = np.array(r.unpack("4f")[:3])
        r.u32()                        # material index
        _ref, t_idx = r.unpack("HH")
        n = r.u32()
        q = np.array(r.unpack(f"{n}H"), float).reshape(-1, 3)
        n = r.u32()
        indices = r.unpack(f"{n}H")
        n = r.u32()
        strips = r.unpack(f"{n}H")
        count = r.u32()
        r.p += 2 * count             # welding info
        verts = q * error + trans
        if t_idx < len(transforms):
            tt, tr = transforms[t_idx]
            verts = verts @ tr.T + tt
        o = 0
        for length in strips:          # triangle strips first...
            for k in range(length - 2):
                a, b, c = indices[o + k], indices[o + k + 1], indices[o + k + 2]
                tris.append([verts[a], verts[b], verts[c]] if k % 2 == 0 else [verts[b], verts[a], verts[c]])
            o += length
        for k in range(o, len(indices) - 2, 3):  # ...then a plain triangle list
            tris.append([verts[indices[k]], verts[indices[k + 1]], verts[indices[k + 2]]])
    return np.array(tris, float).reshape(-1, 3, 3)


def havok_triangles(nif, idx, depth=0):
    """Any supported bhk*Shape -> (N,3,3) triangles in Havok units, or None."""
    if idx < 0 or depth > 8:
        return None
    t = nif.block_types[idx]
    o = nif.block_offsets[idx]
    d = nif.data
    if t == "bhkMoppBvTreeShape":
        return havok_triangles(nif, struct.unpack_from("<i", d, o)[0], depth + 1)
    if t == "bhkCompressedMeshShape":
        data = struct.unpack_from("<i", d, o + 52)[0]
        return _compressed_mesh(nif, data) if data >= 0 else None
    if t == "bhkCapsuleShape":
        p1 = np.array(struct.unpack_from("<3f", d, o + 16))
        r1 = struct.unpack_from("<f", d, o + 28)[0]
        p2 = np.array(struct.unpack_from("<3f", d, o + 32))
        return _capsule(p1, p2, r1)
    if t == "bhkSphereShape":
        r = struct.unpack_from("<f", d, o + 4)[0]
        return _capsule(np.array([0, 0, -r * 0.5]), np.array([0, 0, r * 0.5]), r)
    if t == "bhkBoxShape":
        return _box(np.zeros(3), np.array(struct.unpack_from("<3f", d, o + 16)))
    if t == "bhkConvexVerticesShape":
        r = Reader(d, o + 32)
        verts = np.array([r.unpack("4f")[:3] for _ in range(r.u32())]).reshape(-1, 3)
        planes = np.array([r.unpack("4f") for _ in range(r.u32())]).reshape(-1, 4)
        return _convex_from_planes(verts, planes)
    if t == "bhkListShape":
        r = Reader(d, o)
        parts = [havok_triangles(nif, c, depth + 1) for c in r.unpack(f"{r.u32()}i")]
        parts = [p for p in parts if p is not None and len(p)]
        return np.concatenate(parts) if parts else None
    if t in ("bhkConvexTransformShape", "bhkTransformShape"):
        child = struct.unpack_from("<i", d, o)[0]
        m = np.array(struct.unpack_from("<16f", d, o + 20)).reshape(4, 4).T  # column-major
        tris = havok_triangles(nif, child, depth + 1)
        if tris is None:
            return None
        pts = tris.reshape(-1, 3) @ m[:3, :3].T + m[:3, 3]
        return pts.reshape(-1, 3, 3)
    return None
