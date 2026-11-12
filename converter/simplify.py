"""Mesh simplification for the characters (npcs.py): meshoptimizer's, as the world's meshes
use (meshes.py), keeping each part's borders where they are (texture seams, where clothes meet
skin) so nothing tears.

simplify(positions (N,3), triangles (M,3), ratio, min_triangles)
    -> (kept vertex indices into the input (K,), triangles (T,3) into those K)
Kept vertices are the input's own: their texture coordinates, colours, normals and bone
weights untouched, so a skinned part stays exactly skinned.
"""
import meshoptimizer
import numpy as np

CHARACTER_ERROR = 0.05  # (of the part's size: a hand's knuckles go, its shape stays)


def simplify(positions, triangles, ratio, min_triangles=64):
    pos = np.ascontiguousarray(positions, np.float32).reshape(-1, 3)
    idx = np.ascontiguousarray(triangles, np.uint32).reshape(-1)
    target = max(min_triangles, int(len(idx) // 3 * ratio)) * 3
    if target < len(idx):
        dst = np.zeros_like(idx)
        count = meshoptimizer.simplify(dst, idx, pos, target_index_count=target, target_error=CHARACTER_ERROR,
                                       options=meshoptimizer.SIMPLIFY_LOCK_BORDER)
        if count > 0:
            idx = dst[:count]
    tri = idx.reshape(-1, 3)
    used = np.unique(tri)
    return used, np.searchsorted(used, tri).astype(np.int32)
