#!/usr/bin/env python3
"""Cross-references IFC source openings with VICUS SubSurfaces by 3D position
and checks whether each window/door landed in the SAME room the IFC says it
belongs to.

Quality dimensions:
  * COVERAGE     — % of IfcWindow/IfcDoor that have a VICUS SubSurface within tol
  * POSITION ERROR — distribution of centroid distances for matched pairs
  * WRONG ROOM   — for each matched pair: the IFC source records which room(s)
                   the host wall borders (via FillsVoids→Opening→Voids→Wall +
                   IfcRelSpaceBoundary); if the VICUS Room doesn't match any of
                   them, the opening was attached in the wrong room
  * UNMATCHED    — IFC openings with NO VICUS SubSurface within tol — the real
                   "missing window" case

Usage:
    vicus_opening_match_quality.py --vicus file.vicus --ifc source.ifc \\
        [--tol 0.5] [--detail]
"""
from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path
from typing import Iterable

import ifcopenshell
import ifcopenshell.util.placement


# ---- vector helpers --------------------------------------------------------

def _parse_polyline_2d(text: str) -> list[tuple[float, float]]:
    if not text:
        return []
    out: list[tuple[float, float]] = []
    for chunk in text.replace("\n", " ").strip().split(","):
        parts = chunk.strip().split()
        if len(parts) >= 2:
            out.append((float(parts[0]), float(parts[1])))
    return out


def _parse_vec3(s: str) -> tuple[float, float, float]:
    p = s.split()
    if len(p) < 3:
        return (0.0, 0.0, 0.0)
    return (float(p[0]), float(p[1]), float(p[2]))


def _centroid3d(pts):
    if not pts:
        return (0.0, 0.0, 0.0)
    n = len(pts)
    return (sum(p[0] for p in pts) / n,
            sum(p[1] for p in pts) / n,
            sum(p[2] for p in pts) / n)


def _dist3d(a, b):
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5


# ---- IFC placement (rotation-free origin chain — good for grid-aligned IFCs) ---

def _ifc_object_centroid(elem):
    """World-coordinate origin of the IFC element's ObjectPlacement.
    Uses ifcopenshell.util.placement to walk the IfcLocalPlacement chain
    accumulating the full 4x4 transformation including rotations."""
    placement = getattr(elem, "ObjectPlacement", None)
    if placement is None:
        return None
    try:
        m = ifcopenshell.util.placement.get_local_placement(placement)
    except Exception:
        return None
    # Translation column of the 4x4 matrix
    return (float(m[0][3]), float(m[1][3]), float(m[2][3]))


# ---- IFC topology: which rooms should host this opening? -------------------

def _expected_room_guids(opening_elem) -> set[str]:
    """Returns the set of IfcSpace GlobalIds the opening is expected to land in.

    Strategy:
      a) Direct IfcRelSpaceBoundary on the IfcWindow/IfcDoor → RelatingSpace
      b) IfcRelSpaceBoundary on the host IfcWall (via FillsVoids→Opening→Voids)
         → all RelatingSpaces of those wall-SBs (interior wall borders 2 rooms)
      c) Fallback: IfcRelContainedInSpatialStructure of the host wall → its
         RelatingStructure if that's an IfcSpace
    """
    rooms: set[str] = set()
    # (a)
    sbs_inv = getattr(opening_elem, "ProvidesBoundaries", None) or []
    for sb in sbs_inv:
        sp = getattr(sb, "RelatingSpace", None)
        if sp is not None and sp.is_a("IfcSpace"):
            rooms.add(sp.GlobalId)

    # find host wall
    host_wall = None
    fills_inv = getattr(opening_elem, "FillsVoids", None) or []
    for rel_fills in fills_inv:
        opening_geom = getattr(rel_fills, "RelatingOpeningElement", None)
        if opening_geom is None:
            continue
        for rel_voids in (getattr(opening_geom, "VoidsElements", None) or []):
            host = getattr(rel_voids, "RelatingBuildingElement", None)
            if host is not None:
                host_wall = host
                break
        if host_wall:
            break

    # (b)
    if host_wall is not None:
        wall_sbs = getattr(host_wall, "ProvidesBoundaries", None) or []
        for sb in wall_sbs:
            sp = getattr(sb, "RelatingSpace", None)
            if sp is not None and sp.is_a("IfcSpace"):
                rooms.add(sp.GlobalId)

    return rooms


def _ifc_space_name(f, guid: str) -> str:
    try:
        sp = f.by_guid(guid)
        return (sp.LongName or sp.Name or guid) if sp is not None else guid
    except Exception:
        return guid


# ---- VICUS file parsing ----------------------------------------------------

def _parse_root(vicus_path: Path):
    raw = vicus_path.read_text(encoding="utf-8", errors="replace")
    if "xmlns:IBK=" not in raw:
        raw = raw.replace("<VicusProject ", '<VicusProject xmlns:IBK="urn:local:ibk" ', 1)
    return ET.fromstring(raw)


def _collect_subsurfaces(root, ns: str):
    def q(n: str) -> str:
        return f"{{{ns}}}{n}" if ns else n

    out: list[dict] = []
    for room in root.iter(q("Room")):
        rid = room.attrib.get("id", "?")
        rname = room.attrib.get("displayName", "?")
        rguid = room.attrib.get("ifcGUID", "")
        for surf in room.iter(q("Surface")):
            sname = surf.attrib.get("displayName", "?")
            sid = surf.attrib.get("id", "?")
            sguid = surf.attrib.get("ifcGUID", "")
            poly3d = None
            for c in surf:
                if c.tag == q("Polygon3D"):
                    poly3d = c
                    break
            if poly3d is None:
                continue
            offset = _parse_vec3(poly3d.attrib.get("offset", "0 0 0"))
            normal = _parse_vec3(poly3d.attrib.get("normal", "0 0 1"))
            local_x = _parse_vec3(poly3d.attrib.get("localX", "1 0 0"))
            nx, ny, nz = normal
            lx, ly, lz = local_x
            yx = ny * lz - nz * ly
            yy = nz * lx - nx * lz
            yz = nx * ly - ny * lx
            for sub in surf.iter(q("SubSurface")):
                sub_id = sub.attrib.get("id", "?")
                sub_name = sub.attrib.get("displayName", "?")
                p2d_text = ""
                for c in sub:
                    if c.tag == q("Polygon2D"):
                        p2d_text = c.text or ""
                        break
                p2d = _parse_polyline_2d(p2d_text)
                if not p2d:
                    continue
                pts3d = [(offset[0] + lx * x + yx * y,
                          offset[1] + ly * x + yy * y,
                          offset[2] + lz * x + yz * y) for x, y in p2d]
                out.append({
                    "id": sub_id,
                    "name": sub_name,
                    "centroid": _centroid3d(pts3d),
                    "parent_id": sid,
                    "parent_name": sname,
                    "parent_guid": sguid,
                    "room_id": rid,
                    "room_name": rname,
                    "room_guid": rguid,
                })
    return out


# ---- score formula ---------------------------------------------------------

def compute_score(coverage: float, wrong_room: float, pos_p95: float,
                  bad_quality: float, time_s: float | None) -> dict:
    """Aggregates quality dimensions to a 0-100 overall score.

    All inputs in [0, 1] except time_s which is in seconds; lower is better
    everywhere except `coverage` (higher better).

    Weights (lower-weighted import time per user request):
      coverage     0.30
      wrong_room   0.25 (penalizes openings in the wrong room)
      pos_p95      0.15
      bad_quality  0.20
      import_time  0.10

    pos_p95 is clamped at 2m (a 2m+ centroid offset is fully bad).
    import_time is normalized against 60s — over a minute counts as fully bad.
    """
    # IFC placement-origin offset is ~half the opening diagonal, so a p95 around
    # 1-2m is the floor; normalize against 3m so the "expected" floor scores well.
    pos_norm = min(pos_p95 / 3.0, 1.0)
    if time_s is None:
        time_norm = 0.5  # neutral when unknown
        weight_time = 0.10
    else:
        time_norm = min(time_s / 60.0, 1.0)
        weight_time = 0.10

    score = (
        0.30 * coverage
      + 0.25 * (1.0 - wrong_room)
      + 0.15 * (1.0 - pos_norm)
      + 0.20 * (1.0 - bad_quality)
      + weight_time * (1.0 - time_norm)
    )
    return {
        "overall": 100.0 * score,
        "coverage": coverage,
        "wrong_room": wrong_room,
        "pos_p95": pos_p95,
        "bad_quality": bad_quality,
        "time_s": time_s,
    }


# ---- main ------------------------------------------------------------------

def main(argv: Iterable[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--vicus", required=True, type=Path)
    ap.add_argument("--ifc", required=True, type=Path)
    ap.add_argument("--tol", type=float, default=3.0,
                    help="Centroid-distance tolerance in meters (default 3.0). "
                         "IFC ObjectPlacement is at the entity's anchor point "
                         "(typically a corner), not its geometric centroid; matching "
                         "against VICUS SubSurface centers therefore has a built-in "
                         "offset of half the opening's diagonal. 3m covers most "
                         "windows/doors without false positives across rooms.")
    ap.add_argument("--time-s", type=float, default=None,
                    help="Optional import time in seconds — feeds the unified score")
    ap.add_argument("--detail", action="store_true")
    ap.add_argument("--max-detail", type=int, default=20)
    args = ap.parse_args(argv)

    root = _parse_root(args.vicus)
    ns = root.tag.split("}", 1)[0][1:] if "}" in root.tag else ""
    vicus_subs = _collect_subsurfaces(root, ns)

    f = ifcopenshell.open(str(args.ifc))
    ifc_openings: list[dict] = []
    for kind in ("IfcWindow", "IfcDoor"):
        for elem in f.by_type(kind):
            c = _ifc_object_centroid(elem)
            if c is None:
                continue
            ifc_openings.append({
                "kind": kind,
                "guid": elem.GlobalId,
                "name": elem.Name or "",
                "centroid": c,
                "expected_room_guids": _expected_room_guids(elem),
            })

    # Match each IFC opening to its closest VICUS SubSurface within tol.
    matched: list[tuple[dict, dict, float]] = []
    used_subs: set[int] = set()
    for op in ifc_openings:
        best_idx, best_d = -1, float("inf")
        for i, vs in enumerate(vicus_subs):
            if i in used_subs:
                continue
            d = _dist3d(op["centroid"], vs["centroid"])
            if d < best_d:
                best_d = d
                best_idx = i
        if best_idx >= 0 and best_d <= args.tol:
            matched.append((op, vicus_subs[best_idx], best_d))
            used_subs.add(best_idx)

    n_ifc = len(ifc_openings)
    n_matched = len(matched)
    n_unmatched_ifc = n_ifc - n_matched
    n_orphan_subs = len(vicus_subs) - len(used_subs)

    # Wrong-room: matched IFC opening's expected room set should contain the
    # VICUS Room. Skip openings whose expected set is empty (no IFC SBs and no
    # IfcRelFillsElement) — we can't judge.
    n_judgeable = 0
    n_wrong_room = 0
    wrong_room_examples: list[tuple[dict, dict]] = []
    for op, vs, d in matched:
        expected = op["expected_room_guids"]
        if not expected:
            continue
        n_judgeable += 1
        if vs["room_guid"] and vs["room_guid"] not in expected:
            n_wrong_room += 1
            if len(wrong_room_examples) < args.max_detail:
                wrong_room_examples.append((op, vs))

    distances = sorted(d for _, _, d in matched)
    median = distances[len(distances) // 2] if distances else 0.0
    p95 = distances[int(0.95 * len(distances))] if len(distances) > 20 else (distances[-1] if distances else 0.0)
    max_d = distances[-1] if distances else 0.0

    on_missing = sum(1 for _, vs, _ in matched if vs["parent_name"].startswith("Missing"))
    on_named = n_matched - on_missing

    coverage = n_matched / max(1, n_ifc)
    wrong_room = (n_wrong_room / n_judgeable) if n_judgeable else 0.0
    bad_quality = on_missing / max(1, n_matched)
    score = compute_score(coverage, wrong_room, p95, bad_quality, args.time_s)

    print(f"=== Opening match quality: {args.vicus.name} ===")
    print(f"  IFC openings (Window+Door)  : {n_ifc}")
    print(f"  VICUS SubSurfaces           : {len(vicus_subs)}")
    print(f"  matched (≤{args.tol:.2f}m)         : {n_matched}  ({100.0 * coverage:.1f}%)")
    print(f"  unmatched IFC openings      : {n_unmatched_ifc}")
    print(f"  orphan VICUS SubSurfaces    : {n_orphan_subs}")
    if matched:
        print(f"  centroid offset (m)         : median={median:.3f}  p95={p95:.3f}  max={max_d:.3f}")
    print(f"  in WRONG room               : {n_wrong_room}/{n_judgeable}  ({100.0 * wrong_room:.1f}%)" if n_judgeable else "  WRONG-ROOM check    : (no IFC topology to judge)")
    print(f"  attached to 'Missing' parent: {on_missing}/{n_matched}")
    print(f"")
    print(f"  Import time                 : {args.time_s:.1f}s" if args.time_s is not None else "  Import time         : (not provided)")
    print(f"  =====================================")
    print(f"  OVERALL QUALITY SCORE       : {score['overall']:.1f} / 100")
    print(f"     weights: coverage=0.30  wrong_room=0.25  pos_p95=0.15  bad_quality=0.20  time=0.10")

    if args.detail:
        if wrong_room_examples:
            print(f"\n  Wrong-room examples (first {len(wrong_room_examples)}):")
            for op, vs in wrong_room_examples:
                exp_names = ", ".join(_ifc_space_name(f, g) for g in op["expected_room_guids"])
                print(f"    IFC '{op['name']}' [{op['kind']}] expected in {{{exp_names}}}, found in '{vs['room_name']}'")
        unmatched = [op for op in ifc_openings
                     if not any(m[0] is op for m in matched)]
        if unmatched:
            print(f"\n  Unmatched IFC openings (first {args.max_detail}):")
            for op in unmatched[:args.max_detail]:
                exp = ", ".join(_ifc_space_name(f, g) for g in op["expected_room_guids"]) or "(unknown rooms)"
                print(f"    {op['kind']} '{op['name']}' guid={op['guid']} expected in {{{exp}}}")

    return 0 if (n_unmatched_ifc == 0 and n_wrong_room == 0) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
