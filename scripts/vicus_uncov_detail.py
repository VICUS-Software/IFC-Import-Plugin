#!/usr/bin/env python3
"""Dumps uncovered edges for a specific room (or the worst room) in a VICUS
XML, to diagnose why the room shell is open. For each uncovered edge prints
its 3D endpoints and which surface it belongs to.

Usage:
    vicus_uncov_detail.py --vicus file.vicus [--room ID|--room-name STR]
                          [--eps 0.001]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parent))
from vicus_quality import (Vec3, _iter_surface_polygon3d, _polygon3d_to_world, analyze_vicus)


def _parse_root(vicus_path: Path):
    raw = vicus_path.read_text(encoding="utf-8", errors="replace")
    if 'xmlns:IBK=' not in raw:
        raw = raw.replace(
            "<VicusProject ",
            '<VicusProject xmlns:IBK="urn:local:ibk" ', 1)
    return ET.fromstring(raw)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--vicus", required=True, type=Path)
    ap.add_argument("--room", help="Room id")
    ap.add_argument("--room-name", help="Substring match on displayName")
    ap.add_argument("--eps", type=float, default=1e-3)
    ap.add_argument("--max", type=int, default=50)
    args = ap.parse_args(argv)

    root = _parse_root(args.vicus)
    ns = root.tag.split("}", 1)[0][1:] if "}" in root.tag else ""
    def q(n): return f"{{{ns}}}{n}" if ns else n

    target = None
    candidates = list(root.iter(q("Room")))
    if args.room:
        for r in candidates:
            if r.attrib.get("id") == args.room:
                target = r; break
    elif args.room_name:
        for r in candidates:
            if args.room_name in r.attrib.get("displayName", ""):
                target = r; break
    else:
        # Pick worst room via analyze_vicus
        stats = analyze_vicus(args.vicus)
        worst = sorted(stats["rooms"], key=lambda r: r.uncovered_edges, reverse=True)
        if not worst or worst[0].uncovered_edges == 0:
            print("no open rooms.")
            return 0
        target_id = worst[0].room_id
        for r in candidates:
            if r.attrib.get("id") == target_id:
                target = r; break
    if target is None:
        print("room not found", file=sys.stderr); return 2

    name = target.attrib.get("displayName", "?")
    rid  = target.attrib.get("id", "?")
    print(f"Room id={rid} name={name}")

    surfs = []
    surfaces_container = target.find(q("Surfaces"))
    if surfaces_container is not None:
        for s_elem in surfaces_container.findall(q("Surface")):
            poly = _iter_surface_polygon3d(s_elem)
            verts = _polygon3d_to_world(poly) if poly is not None else []
            surfs.append((s_elem.attrib.get("id", "?"),
                          s_elem.attrib.get("displayName", "?"),
                          verts))

    print(f"  {len(surfs)} surfaces, eps={args.eps}")
    for sid, sname, verts in surfs:
        print(f"  surf id={sid:>6s}  verts={len(verts):>3d}  name={sname!r}")

    # Collect all edges with back-ref to owning surface
    edges = []
    for idx, (sid, sname, verts) in enumerate(surfs):
        n = len(verts)
        for i in range(n):
            edges.append((idx, verts[i], verts[(i+1) % n]))

    # Pair edges: an edge (a,b) matches another (c,d) where a~d and b~c (opposite direction)
    def close(u, v, eps=args.eps):
        return (u-v).norm() < eps
    used = [False] * len(edges)
    uncov = []
    for i, (idx_i, a, b) in enumerate(edges):
        if used[i]:
            continue
        paired = -1
        for j in range(i+1, len(edges)):
            if used[j]:
                continue
            idx_j, c, d = edges[j]
            if (close(a, c) and close(b, d)) or (close(a, d) and close(b, c)):
                paired = j; break
        used[i] = True
        if paired >= 0:
            used[paired] = True
        else:
            uncov.append(i)

    print(f"  uncovered edges: {len(uncov)}")
    for i in uncov[:args.max]:
        idx, a, b = edges[i]
        sid, sname, _ = surfs[idx]
        print(f"    surf#{idx}[{sid}] '{sname}'  "
              f"({a.x:.4f},{a.y:.4f},{a.z:.4f}) -> "
              f"({b.x:.4f},{b.y:.4f},{b.z:.4f})  "
              f"len={(a-b).norm():.4f}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
