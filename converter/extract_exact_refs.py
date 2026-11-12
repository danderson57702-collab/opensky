#!/usr/bin/env python3
"""Targeted extractor for exact float XYZ coordinates, cell grids, and rotation vectors
from Skyrim.esm (WRLD and REFR record blocks).

Extracts bit-for-bit exact IEEE-754 single-precision float coordinates without precision
loss, avoiding the coordinate scaling (1/70.0 m) and rotation matrix conversion (Euler -> 3x3)
performed by engine export pipelines.

Usage:
  python3 extract_exact_refs.py [--helgen] [--world WORLD] [--all-worlds] [--esm PATH]
                                [--bounds MIN_X MIN_Y MAX_X MAX_Y]
                                [--output FILE.json] [--csv FILE.csv] [--validate]
"""

import argparse
import csv
import json
import math
import os
import struct
import sys

# Ensure converter package is on path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from esm import ESM, GROUP, CELL_UNITS, RECORD_DELETED, RECORD_INITIALLY_DISABLED

# The game's Data folder: the user's own (SKYRIM_DATA, or converter/data_path.txt - never one in the source)
DEFAULT_DATA_PATH = os.environ.get("SKYRIM_DATA") or (
    open(os.path.join(SCRIPT_DIR, "data_path.txt")).read().strip() if os.path.exists(os.path.join(SCRIPT_DIR, "data_path.txt")) else "")

# Helgen Exterior Cells in Tamriel: (grid_x, grid_y) -> Cell EditorID
HELGEN_EXTERIOR_CELLS = {
    (2, -17): "HelgenExteriorExit",
    (3, -18): "HelgenExterior04",
    (4, -19): "HelgenExterior",
    (3, -19): "HelgenExterior05",
    (4, -20): "HelgenExterior02",
    (3, -20): "HelgenExterior06",
    (4, -21): "HelgenExterior03",
    (3, -21): "HelgenExterior07",
    (1, -16): "HelgenRiverwoodCrossroads",
}

# Major Exterior Worldspaces in Skyrim
MAJOR_EXTERIOR_WORLDS = [
    "Tamriel",
    "WhiterunWorld",
    "SolitudeWorld",
    "WindhelmWorld",
    "RiftenWorld",
    "MarkarthWorld",
    "Blackreach",
    "Sovngarde",
    "SkuldafnWorld",
    "LabyrinthianWorld",
    "BloatedMansGrottoWorld",
    "MossMotherCavernWorld",
]


def find_skyrim_esm(custom_path=None):
    """Locates Skyrim.esm using custom path, default data path, or data_path.txt."""
    if custom_path and os.path.isfile(custom_path):
        return custom_path

    txt_cfg = os.path.join(SCRIPT_DIR, "data_path.txt")
    if os.path.exists(txt_cfg):
        with open(txt_cfg) as f:
            p = os.path.join(f.read().strip(), "Skyrim.esm")
            if os.path.isfile(p):
                return p

    default_esm = os.path.join(DEFAULT_DATA_PATH, "Skyrim.esm")
    if os.path.isfile(default_esm):
        return default_esm

    raise FileNotFoundError("Could not locate Skyrim.esm. Please specify --esm PATH.")


def parse_world_metadata(world_rec):
    """Parses all WRLD subrecords without precision loss."""
    if world_rec is None:
        return {}

    meta = {
        "form_id": f"0x{world_rec.form_id:08X}",
        "form_id_int": world_rec.form_id,
        "editor_id": world_rec.editor_id(),
        "flags": f"0x{world_rec.flags:08X}",
    }

    # Display name
    full = world_rec.sub("FULL")
    if full:
        meta["full_name"] = full.rstrip(b"\0").decode("latin1", errors="replace")

    # Default land and water height (DNAM, NAM4)
    dnam = world_rec.sub("DNAM")
    if dnam and len(dnam) >= 8:
        land_h, water_h = struct.unpack("<2f", dnam[:8])
        meta["default_land_height"] = land_h
        meta["default_water_height"] = water_h
        meta["default_land_height_hex"] = land_h.hex()
        meta["default_water_height_hex"] = water_h.hex()

    nam4 = world_rec.sub("NAM4")
    if nam4 and len(nam4) >= 4:
        w_h = struct.unpack("<f", nam4[:4])[0]
        meta["nam4_water_height"] = w_h
        meta["nam4_water_height_hex"] = w_h.hex()

    # World bounds (NAM0 min, NAM9 max)
    nam0 = world_rec.sub("NAM0")
    if nam0 and len(nam0) >= 8:
        x0, y0 = struct.unpack("<2f", nam0[:8])
        meta["min_coords"] = [x0, y0]
        meta["min_coords_hex"] = [x0.hex(), y0.hex()]

    nam9 = world_rec.sub("NAM9")
    if nam9 and len(nam9) >= 8:
        x9, y9 = struct.unpack("<2f", nam9[:8])
        meta["max_coords"] = [x9, y9]
        meta["max_coords_hex"] = [x9.hex(), y9.hex()]

    # Map dimensions and cell boundaries (MNAM)
    mnam = world_rec.sub("MNAM")
    if mnam and len(mnam) >= 16:
        dim_x, dim_y, min_cx, min_cy, max_cx, max_cy = struct.unpack("<2I4h", mnam[:16])
        meta["usable_dim"] = [dim_x, dim_y]
        meta["cell_bounds"] = {
            "min_grid": [min_cx, min_cy],
            "max_grid": [max_cx, max_cy],
        }

    # Center cell (WCTR)
    wctr = world_rec.sub("WCTR")
    if wctr and len(wctr) >= 4:
        cx, cy = struct.unpack("<2h", wctr[:4])
        meta["center_cell"] = [cx, cy]

    # Parent worldspace (WNAM / PNAM)
    wnam = world_rec.sub("WNAM")
    if wnam and len(wnam) >= 4:
        parent_id = struct.unpack("<I", wnam[:4])[0]
        meta["parent_world_form_id"] = f"0x{parent_id:08X}"

    pnam = world_rec.sub("PNAM")
    if pnam and len(pnam) >= 2:
        meta["parent_flags"] = struct.unpack("<H", pnam[:2])[0]

    # Map scale (NAMA / ONAM)
    nama = world_rec.sub("NAMA")
    if nama and len(nama) >= 4:
        s = struct.unpack("<f", nama[:4])[0]
        meta["map_scale"] = s
        meta["map_scale_hex"] = s.hex()

    return meta


def extract_ref_data(ref, cell_grid, cell_editor_id=None, base_lookup=None):
    """Extracts exact float XYZ coordinates, cell grid, rotation vector, and metadata
    from a placed reference (REFR/ACHR) without precision loss.
    """
    data = ref.sub("DATA")
    if not data or len(data) < 24:
        return None

    # Exact IEEE-754 single precision float unpacking
    px, py, pz, rx, ry, rz = struct.unpack("<6f", data[:24])

    # Scale (XSCL)
    xscl = ref.sub("XSCL")
    scale = struct.unpack("<f", xscl[:4])[0] if xscl and len(xscl) >= 4 else 1.0

    # Base Object FormID (NAME)
    name = ref.sub("NAME")
    base_id = struct.unpack("<I", name[:4])[0] if name and len(name) >= 4 else 0
    base_info = base_lookup(base_id) if base_lookup else {}

    # Cell coordinates: ground truth from XCLC vs computed from float
    calc_gx = math.floor(px / CELL_UNITS)
    calc_gy = math.floor(py / CELL_UNITS)

    ref_dict = {
        "form_id": f"0x{ref.form_id:08X}",
        "form_id_int": ref.form_id,
        "editor_id": ref.editor_id() or "",
        "record_type": ref.type,
        "flags": {
            "raw": f"0x{ref.flags:08X}",
            "initially_disabled": bool(ref.flags & RECORD_INITIALLY_DISABLED),
            "deleted": bool(ref.flags & RECORD_DELETED),
        },
        "cell_grid": list(cell_grid) if cell_grid else [calc_gx, calc_gy],
        "calc_grid": [calc_gx, calc_gy],
        "cell_editor_id": cell_editor_id or "",
        "base": {
            "form_id": f"0x{base_id:08X}",
            "form_id_int": base_id,
            "type": base_info.get("type", ""),
            "editor_id": base_info.get("editor_id", ""),
        },
        "coordinates": {
            "x": px,
            "y": py,
            "z": pz,
            # IEEE-754 hexadecimal float representation guarantees 100% lossless roundtrip
            "hex": {
                "x": px.hex(),
                "y": py.hex(),
                "z": pz.hex(),
            },
            "raw_bytes_hex": data[:12].hex(),
        },
        "rotation": {
            "rx": rx,
            "ry": ry,
            "rz": rz,
            "degrees": {
                "rx": math.degrees(rx),
                "ry": math.degrees(ry),
                "rz": math.degrees(rz),
            },
            "hex": {
                "rx": rx.hex(),
                "ry": ry.hex(),
                "rz": rz.hex(),
            },
            "raw_bytes_hex": data[12:24].hex(),
        },
        "scale": scale,
        "scale_hex": scale.hex(),
    }

    # Teleport destination for doors (XTEL)
    xtel = ref.sub("XTEL")
    if xtel and len(xtel) >= 28:
        dest_door = struct.unpack("<I", xtel[:4])[0]
        dpx, dpy, dpz, drx, dry, drz = struct.unpack("<6f", xtel[4:28])
        ref_dict["teleport"] = {
            "dest_door_form_id": f"0x{dest_door:08X}",
            "dest_pos": [dpx, dpy, dpz],
            "dest_pos_hex": [dpx.hex(), dpy.hex(), dpz.hex()],
            "dest_rot": [drx, dry, drz],
            "dest_rot_deg": [math.degrees(drx), math.degrees(dry), math.degrees(drz)],
        }

    # Primitive bounds for triggers / collision markers (XPRM)
    xprm = ref.sub("XPRM")
    if xprm and len(xprm) >= 12:
        hx, hy, hz = struct.unpack("<3f", xprm[:12])
        ref_dict["primitive"] = {
            "half_extents": [hx, hy, hz],
            "half_extents_hex": [hx.hex(), hy.hex(), hz.hex()],
        }

    return ref_dict


def build_base_lookup(esm):
    """Builds an index of base records to resolve placed references."""
    index = {}
    base_types = ("STAT", "MSTT", "TREE", "FLOR", "FURN", "DOOR", "CONT", "ACTI", "LIGH", "NPC_")
    for rtype in base_types:
        for rec in esm.records_of_type(rtype):
            index[rec.form_id] = {
                "type": rec.type,
                "editor_id": rec.editor_id(),
            }
    return lambda fid: index.get(fid, {})


def validate_exact_precision(refs_data, esm):
    """Validates bit-for-bit exactness: repacks coordinates and verifies against raw bytes."""
    print("\nRunning bit-for-bit precision validation...")
    tested = 0
    errors = 0

    for r in refs_data:
        px = r["coordinates"]["x"]
        py = r["coordinates"]["y"]
        pz = r["coordinates"]["z"]
        rx = r["rotation"]["rx"]
        ry = r["rotation"]["ry"]
        rz = r["rotation"]["rz"]

        # Validate hex roundtrip
        assert float.fromhex(r["coordinates"]["hex"]["x"]) == px, "X float hex roundtrip failed!"
        assert float.fromhex(r["coordinates"]["hex"]["y"]) == py, "Y float hex roundtrip failed!"
        assert float.fromhex(r["coordinates"]["hex"]["z"]) == pz, "Z float hex roundtrip failed!"
        assert float.fromhex(r["rotation"]["hex"]["rx"]) == rx, "RX float hex roundtrip failed!"
        assert float.fromhex(r["rotation"]["hex"]["ry"]) == ry, "RY float hex roundtrip failed!"
        assert float.fromhex(r["rotation"]["hex"]["rz"]) == rz, "RZ float hex roundtrip failed!"

        # Pack to IEEE-754 32-bit floats
        packed = struct.pack("<6f", px, py, pz, rx, ry, rz)
        raw_expected = bytes.fromhex(r["coordinates"]["raw_bytes_hex"] + r["rotation"]["raw_bytes_hex"])

        if packed != raw_expected:
            errors += 1
            print(f"Mismatch in Ref {r['form_id']}: packed {packed.hex()} != raw {raw_expected.hex()}")
        tested += 1

    print(f"Validation complete: {tested} references tested, {errors} errors.")
    return errors == 0


def extract_helgen(esm, base_lookup):
    """Extracts all exterior references for Helgen's cells."""
    print("Extracting Helgen exterior cells from Tamriel...")
    tamriel_cells = esm.exterior_cells("Tamriel", keep_removed=True)
    world_rec = esm.world_record("Tamriel")
    world_meta = parse_world_metadata(world_rec)

    extracted_refs = []
    cells_summary = []

    for grid in sorted(HELGEN_EXTERIOR_CELLS.keys()):
        cell_name = HELGEN_EXTERIOR_CELLS[grid]
        cell_data = tamriel_cells.get(grid)
        if not cell_data:
            continue

        cell_rec = cell_data["cell"]
        refs = cell_data["refs"]

        cells_summary.append({
            "grid": list(grid),
            "editor_id": cell_name,
            "form_id": f"0x{cell_rec.form_id:08X}" if cell_rec else None,
            "ref_count": len(refs),
        })

        for ref in refs:
            item = extract_ref_data(ref, grid, cell_name, base_lookup)
            if item:
                extracted_refs.append(item)

    return {
        "target": "Helgen",
        "worldspace": "Tamriel",
        "world_metadata": world_meta,
        "cells": cells_summary,
        "total_references": len(extracted_refs),
        "references": extracted_refs,
    }


def extract_world(esm, world_name, base_lookup, bounds=None, keep_removed=True):
    """Extracts exterior references for a specified worldspace."""
    print(f"Extracting worldspace '{world_name}'...")
    world_rec = esm.world_record(world_name)
    world_meta = parse_world_metadata(world_rec)

    try:
        cells = esm.exterior_cells(world_name, keep_removed=keep_removed)
    except KeyError:
        print(f"Worldspace '{world_name}' has no exterior children group.")
        return {
            "target": world_name,
            "world_metadata": world_meta,
            "cells": [],
            "total_references": 0,
            "references": [],
        }

    min_x, min_y, max_x, max_y = bounds if bounds else (None, None, None, None)

    extracted_refs = []
    cells_summary = []

    for grid, cell_data in sorted(cells.items()):
        gx, gy = grid
        if min_x is not None and not (min_x <= gx <= max_x and min_y <= gy <= max_y):
            continue

        cell_rec = cell_data["cell"]
        cell_edid = cell_rec.editor_id() if cell_rec else ""
        refs = cell_data["refs"]

        cells_summary.append({
            "grid": list(grid),
            "editor_id": cell_edid,
            "form_id": f"0x{cell_rec.form_id:08X}" if cell_rec else None,
            "ref_count": len(refs),
        })

        for ref in refs:
            item = extract_ref_data(ref, grid, cell_edid, base_lookup)
            if item:
                extracted_refs.append(item)

    return {
        "target": world_name,
        "world_metadata": world_meta,
        "bounds_filter": bounds,
        "cells_count": len(cells_summary),
        "total_references": len(extracted_refs),
        "references": extracted_refs,
    }


def extract_all_worlds_summary(esm):
    """Audits and extracts high-level metadata for all 37 exterior worldspaces."""
    print("Auditing all exterior worldspaces in Skyrim.esm...")
    all_worlds = esm.world_records()
    summary = []

    for w in all_worlds:
        edid = w.editor_id()
        meta = parse_world_metadata(w)
        try:
            cells = esm.exterior_cells(edid, keep_removed=True)
            cell_count = len(cells)
            ref_count = sum(len(c["refs"]) for c in cells.values())
        except KeyError:
            cell_count = 0
            ref_count = 0

        meta["exterior_cells_count"] = cell_count
        meta["exterior_refs_count"] = ref_count
        summary.append(meta)

    return summary


def write_csv(refs, csv_path):
    """Writes extracted references to a clean CSV file."""
    if not refs:
        print("No references to write to CSV.")
        return

    fieldnames = [
        "ref_form_id", "editor_id", "record_type", "initially_disabled", "deleted",
        "grid_x", "grid_y", "cell_editor_id",
        "base_form_id", "base_type", "base_editor_id",
        "pos_x", "pos_y", "pos_z", "pos_x_hex", "pos_y_hex", "pos_z_hex",
        "rot_x_rad", "rot_y_rad", "rot_z_rad",
        "rot_x_deg", "rot_y_deg", "rot_z_deg",
        "scale", "scale_hex",
    ]

    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for r in refs:
            writer.writerow({
                "ref_form_id": r["form_id"],
                "editor_id": r["editor_id"],
                "record_type": r["record_type"],
                "initially_disabled": r["flags"]["initially_disabled"],
                "deleted": r["flags"]["deleted"],
                "grid_x": r["cell_grid"][0],
                "grid_y": r["cell_grid"][1],
                "cell_editor_id": r["cell_editor_id"],
                "base_form_id": r["base"]["form_id"],
                "base_type": r["base"]["type"],
                "base_editor_id": r["base"]["editor_id"],
                "pos_x": r["coordinates"]["x"],
                "pos_y": r["coordinates"]["y"],
                "pos_z": r["coordinates"]["z"],
                "pos_x_hex": r["coordinates"]["hex"]["x"],
                "pos_y_hex": r["coordinates"]["hex"]["y"],
                "pos_z_hex": r["coordinates"]["hex"]["z"],
                "rot_x_rad": r["rotation"]["rx"],
                "rot_y_rad": r["rotation"]["ry"],
                "rot_z_rad": r["rotation"]["rz"],
                "rot_x_deg": r["rotation"]["degrees"]["rx"],
                "rot_y_deg": r["rotation"]["degrees"]["ry"],
                "rot_z_deg": r["rotation"]["degrees"]["rz"],
                "scale": r["scale"],
                "scale_hex": r["scale_hex"],
            })
    print(f"Wrote {len(refs)} references to CSV: {csv_path}")


def print_report(data):
    """Prints a structured summary of the extraction."""
    print("=" * 80)
    print(f" EXTRACTION REPORT: {data.get('target', 'Worldspaces')}")
    print("=" * 80)

    meta = data.get("world_metadata")
    if meta:
        print(f"Worldspace:      {meta.get('editor_id')} ({meta.get('form_id')})")
        if "default_land_height" in meta:
            print(f"Land Height:     {meta['default_land_height']:.2f} (hex: {meta['default_land_height_hex']})")
        if "default_water_height" in meta:
            print(f"Water Height:    {meta['default_water_height']:.2f} (hex: {meta['default_water_height_hex']})")
        if "cell_bounds" in meta:
            print(f"Cell Bounds:     Min {meta['cell_bounds']['min_grid']} -> Max {meta['cell_bounds']['max_grid']}")

    cells = data.get("cells", [])
    if cells:
        print(f"\nExtracted Cells ({len(cells)} cells):")
        for c in cells:
            print(f"  Grid {tuple(c['grid'])}: {c['editor_id']:<30} {c['ref_count']:>5} refs (FormID: {c['form_id']})")

    refs = data.get("references", [])
    print(f"\nTotal Placed References Extracted: {len(refs)}")

    if refs:
        print("\nSample Placed References (exact bit-for-bit float representation):")
        for r in refs[:6]:
            c = r["coordinates"]
            rot = r["rotation"]
            b = r["base"]
            print(f"\n  Ref {r['form_id']} ({r['editor_id'] or 'Unnamed'}) | Base: {b['form_id']} [{b['type']} {b['editor_id']}]")
            print(f"    Cell Grid: {tuple(r['cell_grid'])} ({r['cell_editor_id']})")
            print(f"    Position:  X={c['x']:.8f}, Y={c['y']:.8f}, Z={c['z']:.8f}")
            print(f"               hex: ({c['hex']['x']}, {c['hex']['y']}, {c['hex']['z']})")
            print(f"    Rotation:  rx={rot['rx']:.8f} rad, ry={rot['ry']:.8f} rad, rz={rot['rz']:.8f} rad")
            print(f"               deg: ({rot['degrees']['rx']:.4f}°, {rot['degrees']['ry']:.4f}°, {rot['degrees']['rz']:.4f}°)")
            print(f"    Scale:     {r['scale']:.6f} (hex: {r['scale_hex']})")
    print("=" * 80)


def main():
    parser = argparse.ArgumentParser(description="Extract exact float XYZ coordinates and rotation vectors from Skyrim.esm.")
    parser.add_argument("--esm", help="Path to Skyrim.esm")
    parser.add_argument("--helgen", action="store_true", help="Extract Helgen exterior cells (default)")
    parser.add_argument("--world", help="Extract a specific exterior worldspace (e.g. WhiterunWorld, Tamriel)")
    parser.add_argument("--all-worlds", action="store_true", help="Audit all 37 exterior worldspaces")
    parser.add_argument("--bounds", type=int, nargs=4, metavar=("MIN_X", "MIN_Y", "MAX_X", "MAX_Y"),
                        help="Bounding box of exterior cells to extract")
    parser.add_argument("--output", "-o", help="Output JSON file path")
    parser.add_argument("--csv", help="Output CSV file path")
    parser.add_argument("--validate", action="store_true", help="Run bit-for-bit precision roundtrip validation")

    args = parser.parse_args()

    esm_path = find_skyrim_esm(args.esm)
    print(f"Loading ESM: {esm_path}...")
    esm = ESM(esm_path)
    base_lookup = build_base_lookup(esm)

    if args.all_worlds:
        summary = extract_all_worlds_summary(esm)
        print(f"\nTotal exterior worldspaces audited: {len(summary)}")
        print(f"{'Editor ID':<30} {'FormID':<12} {'Cells':<8} {'Refs':<10} {'Water Height':<15}")
        print("-" * 75)
        for w in summary:
            wh = f"{w.get('default_water_height', 0.0):.1f}" if 'default_water_height' in w else "N/A"
            print(f"{w['editor_id']:<30} {w['form_id']:<12} {w.get('exterior_cells_count', 0):<8} {w.get('exterior_refs_count', 0):<10} {wh:<15}")

        if args.output:
            with open(args.output, "w", encoding="utf-8") as f:
                json.dump(summary, f, indent=2)
            print(f"Wrote all worldspaces summary to {args.output}")
        return 0

    if args.world:
        data = extract_world(esm, args.world, base_lookup, bounds=args.bounds)
    else:
        # Default to Helgen
        data = extract_helgen(esm, base_lookup)

    print_report(data)

    if args.validate:
        valid = validate_exact_precision(data["references"], esm)
        if not valid:
            print("ERROR: Precision validation failed!")
            return 1

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)
        print(f"Wrote exact JSON data to {args.output}")

    if args.csv:
        write_csv(data["references"], args.csv)

    return 0


if __name__ == "__main__":
    sys.exit(main())
