"""The words the Greybeards shout onto the floor (The Way of the Voice, the Horn's return, Clear
Skies): Skyrim's effects\\greybeardwords\\fxgreybeardshout<word>.nif - its letters, glowing
(the shape with the dragon alphabet, textures\\dungeons\\dragonrunealphabet.dds, as its
effect texture: a quad per letter). The engine (wordfx.c) lays them on the floor before you.

  PACK_DIR/objects/words.txt
    W word vertices triangles      a word (lowercase), then its lines:
    V x y z u v                    a vertex (engine metres about the word's centre; y up)
    T a b c                        a triangle

python3 greybeard_words.py ../pack
"""
import os
import sys

WORDS = ("dah", "dun", "faas", "feim", "fus", "grah", "gron", "kest", "koor", "krein", "lok", "maar", "nah", "ro", "ru",
         "shul", "su", "toor", "vah", "wuld", "yol", "zii")
ALPHABET = "textures\\dungeons\\dragonrunealphabet.dds"  # (the glowing letters: the shape the alphabet's an effect on)
UNITS_PER_METER = 70.0


def build(pack, archives=None, find=None):
    from nif import NIF
    if archives is None:
        from extract_meshes import open_mesh_archives
        from extract_textures import find as find_in
        archives, find = open_mesh_archives(), find_in
    out, written = [], 0
    for w in WORDS:
        path = f"meshes\\effects\\greybeardwords\\fxgreybeardshout{w}.nif"
        try:
            shapes = NIF(find(archives, path)).shapes()
        except Exception:
            continue
        letters = next((s for s in shapes if (s.effect_texture or "").lower() == ALPHABET), None)
        if letters is None or len(letters.positions) == 0:
            continue
        verts = [(x / UNITS_PER_METER, z / UNITS_PER_METER, -y / UNITS_PER_METER) for x, y, z in letters.positions]
        out.append(f"W {w} {len(verts)} {len(letters.triangles)}")
        out += [f"V {x:.4f} {y:.4f} {z:.4f} {u:.5f} {v:.5f}" for (x, y, z), (u, v) in zip(verts, letters.uvs)]
        out += [f"T {a} {b} {c}" for a, b, c in letters.triangles]
        written += 1
    with open(os.path.join(pack, "objects", "words.txt"), "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"greybeard words: {written} of {len(WORDS)}")


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    build(sys.argv[1] if len(sys.argv) > 1 else "../pack")
