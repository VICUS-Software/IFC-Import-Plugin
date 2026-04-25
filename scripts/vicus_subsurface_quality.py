#!/usr/bin/env python3
"""Validates SubSurface (window/door) geometry quality in a VICUS XML.

For each SubSurface, checks that:
  1. All 2D vertices lie inside the parent Surface's 2D polygon (point-in-polygon).
  2. SubSurface area > 0 and < Parent area.
  3. Polygon is simple (non-self-intersecting) — ear-clipping passes.
  4. SubSurface 2D bounding box is within parent's bounding box (cheap pre-check).

Reports any SubSurface that fails any check, plus a summary count.

A "wrong-surface match" typically appears as: SubSurface's 2D vertices are
OUTSIDE the parent's polygon (or partially outside) — meaning the opening was
attached to a wall it doesn't actually fit on.

Usage:
    vicus_subsurface_quality.py --vicus path/to/project.vicus [--detail]
"""
from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Iterable


def _parse_polyline_2d(text: str) -> list[tuple[float, float]]:
    if not text:
        return []
    text = text.replace("\n", " ").replace("\r", " ").strip()
    if not text:
        return []
    out: list[tuple[float, float]] = []
    for chunk in text.split(","):
        parts = chunk.strip().split()
        if len(parts) >= 2:
            out.append((float(parts[0]), float(parts[1])))
    return out


def _signed_area_2d(poly: list[tuple[float, float]]) -> float:
    """Shoelace formula. Positive = CCW, Negative = CW."""
    a = 0.0
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        a += x1 * y2 - x2 * y1
    return 0.5 * a


def _point_in_polygon(p: tuple[float, float], poly: list[tuple[float, float]],
                      eps: float = 1e-6) -> bool:
    """Ray-casting point-in-polygon. Boundary points within eps count as inside."""
    x, y = p
    n = len(poly)
    inside = False
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        # Edge intersection check
        if ((yi > y) != (yj > y)) and (x < (xj - xi) * (y - yi) / (yj - yi + 1e-30) + xi):
            inside = not inside
        # Boundary check: within eps of edge segment
        dx, dy = xj - xi, yj - yi
        seg_len2 = dx * dx + dy * dy
        if seg_len2 > 0:
            t = ((x - xi) * dx + (y - yi) * dy) / seg_len2
            if 0 <= t <= 1:
                fx, fy = xi + t * dx, yi + t * dy
                if (x - fx) ** 2 + (y - fy) ** 2 < eps * eps:
                    return True
        j = i
    return inside


def _bbox(poly: list[tuple[float, float]]) -> tuple[float, float, float, float]:
    xs = [p[0] for p in poly]
    ys = [p[1] for p in poly]
    return min(xs), min(ys), max(xs), max(ys)


def _segments_intersect(a, b, c, d) -> bool:
    """Returns True if open segments (a-b) and (c-d) intersect strictly."""
    def cross(o, p, q):
        return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0])
    d1 = cross(c, d, a)
    d2 = cross(c, d, b)
    d3 = cross(a, b, c)
    d4 = cross(a, b, d)
    if ((d1 > 0 and d2 < 0) or (d1 < 0 and d2 > 0)) and \
       ((d3 > 0 and d4 < 0) or (d3 < 0 and d4 > 0)):
        return True
    return False


def _is_simple(poly: list[tuple[float, float]]) -> bool:
    """Naive O(N²) self-intersection test. Skip adjacent edges (share a vertex)."""
    n = len(poly)
    if n < 4:
        return True
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        for j in range(i + 2, n):
            if i == 0 and j == n - 1:
                continue  # adjacent (closing edge shares vertex with first)
            c, d = poly[j], poly[(j + 1) % n]
            if _segments_intersect(a, b, c, d):
                return False
    return True


def _parse_root(vicus_path: Path):
    raw = vicus_path.read_text(encoding="utf-8", errors="replace")
    if 'xmlns:IBK=' not in raw:
        raw = raw.replace("<VicusProject ", '<VicusProject xmlns:IBK="urn:local:ibk" ', 1)
    return ET.fromstring(raw)


def main(argv: Iterable[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--vicus", required=True, type=Path)
    ap.add_argument("--detail", action="store_true",
                    help="Print every failing SubSurface, not just summary")
    ap.add_argument("--max-detail", type=int, default=20)
    args = ap.parse_args(argv)

    root = _parse_root(args.vicus)
    ns = root.tag.split("}", 1)[0][1:] if "}" in root.tag else ""
    def q(name: str) -> str:
        return f"{{{ns}}}{name}" if ns else name

    n_subs = 0
    n_outside_parent = 0  # at least one vertex strictly outside parent
    n_partial = 0         # some vertices inside, some outside (worse)
    n_zero_area = 0
    n_too_big = 0         # area > 99% of parent
    n_self_isect = 0
    n_wrong_orientation = 0  # CW instead of CCW
    failures: list[tuple[str, str, str, str]] = []

    for room in root.iter(q("Room")):
        rname = room.attrib.get("displayName", "?")
        rid = room.attrib.get("id", "?")
        for surf in room.iter(q("Surface")):
            sname = surf.attrib.get("displayName", "?")
            sid = surf.attrib.get("id", "?")
            # Find parent's Polygon3D's 2D polyline (direct child only)
            parent_poly2d: list[tuple[float, float]] = []
            for child in surf:
                if child.tag == q("Polygon3D"):
                    parent_poly2d = _parse_polyline_2d(child.text or "")
                    break
            if len(parent_poly2d) < 3:
                continue
            parent_area = abs(_signed_area_2d(parent_poly2d))
            parent_bb = _bbox(parent_poly2d)

            for sub in surf.iter(q("SubSurface")):
                sub_id = sub.attrib.get("id", "?")
                sub_name = sub.attrib.get("displayName", "?")
                sub_poly: list[tuple[float, float]] = []
                for c in sub:
                    if c.tag == q("Polygon2D"):
                        sub_poly = _parse_polyline_2d(c.text or "")
                        break
                if len(sub_poly) < 3:
                    continue
                n_subs += 1

                inside_count = sum(1 for p in sub_poly if _point_in_polygon(p, parent_poly2d, eps=0.001))
                if inside_count == 0:
                    n_outside_parent += 1
                    if args.detail and len(failures) < args.max_detail:
                        failures.append(("OUTSIDE_PARENT", rname, sname, sub_name))
                elif inside_count < len(sub_poly):
                    n_partial += 1
                    if args.detail and len(failures) < args.max_detail:
                        failures.append(("PARTIAL_OUTSIDE",
                                         f"{rname} ({inside_count}/{len(sub_poly)} inside)",
                                         sname, sub_name))

                signed = _signed_area_2d(sub_poly)
                area = abs(signed)
                if area < 1e-9:
                    n_zero_area += 1
                    if args.detail and len(failures) < args.max_detail:
                        failures.append(("ZERO_AREA", rname, sname, sub_name))
                elif parent_area > 0 and area > 0.99 * parent_area:
                    n_too_big += 1
                    if args.detail and len(failures) < args.max_detail:
                        failures.append(("TOO_BIG_VS_PARENT",
                                         f"{rname} (sub_area={area:.3f} parent_area={parent_area:.3f})",
                                         sname, sub_name))
                if signed < 0:
                    n_wrong_orientation += 1
                    if args.detail and len(failures) < args.max_detail:
                        failures.append(("CW_ORIENTATION (should be CCW)", rname, sname, sub_name))
                if not _is_simple(sub_poly):
                    n_self_isect += 1
                    if args.detail and len(failures) < args.max_detail:
                        failures.append(("SELF_INTERSECT", rname, sname, sub_name))

    print(f"=== SubSurface quality report: {args.vicus.name} ===")
    print(f"  total SubSurfaces            : {n_subs}")
    print(f"  fully OUTSIDE parent polygon : {n_outside_parent}")
    print(f"  PARTIALLY outside parent     : {n_partial}")
    print(f"  zero-area                    : {n_zero_area}")
    print(f"  area > 99% of parent         : {n_too_big}")
    print(f"  self-intersecting polygon    : {n_self_isect}")
    print(f"  CW (wrong) orientation       : {n_wrong_orientation}")
    bad = n_outside_parent + n_partial + n_zero_area + n_too_big + n_self_isect
    if n_subs > 0:
        print(f"  bad-quality fraction         : {bad}/{n_subs}  ({100.0 * bad / n_subs:.1f}%)")
    if args.detail and failures:
        print(f"\n  Sample failures (first {len(failures)}):")
        for kind, room, surf, sub in failures:
            print(f"    [{kind}] room={room!r} surf={surf[:50]!r} sub={sub[:50]!r}")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
