#!/usr/bin/env python3
"""VICUS import quality analyzer.

Parses a VICUS project XML (produced by IFC2BESTest CLI or the SIM-VICUS plugin)
and reports quality metrics relevant for IFC → VICUS conversion:

  * Rooms count, surface count per room, cumulative area/volume
  * For each room: uncovered-edge count (rooms with uncovered edges do not form a
    closed shell — equivalent to VICUS::Room::isVolumeOpen()).
  * Sub-surface count (openings matched onto walls → windows/doors).
  * Shading object + shading-surface count.
  * Cross-reference IFC → VICUS: counts IfcWindow / IfcDoor / IfcOpeningElement
    in the source IFC and reports the matching rate.

The uncovered-edge detection mirrors the C++ implementation in VICUS_Room.cpp:
an edge (pair of 3D points) is "covered" if another edge in the same room shares
both endpoints within a small tolerance — otherwise it is uncovered. If any
uncovered edge remains, the room volume is not closed.

Usage:
    vicus_quality.py --vicus path/to/project.vicus [--ifc path/to/source.ifc]

Exit code: 0 if everything passed, 1 on hard parse errors.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

# Tolerances mirror VICUS_Room.cpp (namespace anon section ~line 286):
#   ABS_TOLERANCE           = 3 cm — point/edge proximity
#   MIN_OPEN_EDGE_LENGTH    = 5 cm — uncovered segments shorter than this are
#                                     treated as floating-point noise, not real
#                                     openings, so the room still counts as closed
#   ANGLE_PARALLEL_TOL_RAD  ≈ 5°   — two edges are "parallel enough" to check
#                                     for interval overlap
EDGE_EPS               = 0.03
MIN_OPEN_EDGE_LENGTH   = 0.05
ANGLE_PARALLEL_TOL_RAD = 0.0872665  # ~5°


@dataclass
class Vec3:
    x: float
    y: float
    z: float

    def __add__(self, other: "Vec3") -> "Vec3":
        return Vec3(self.x + other.x, self.y + other.y, self.z + other.z)

    def __sub__(self, other: "Vec3") -> "Vec3":
        return Vec3(self.x - other.x, self.y - other.y, self.z - other.z)

    def __mul__(self, s: float) -> "Vec3":
        return Vec3(self.x * s, self.y * s, self.z * s)

    def dot(self, other: "Vec3") -> float:
        return self.x * other.x + self.y * other.y + self.z * other.z

    def cross(self, other: "Vec3") -> "Vec3":
        return Vec3(
            self.y * other.z - self.z * other.y,
            self.z * other.x - self.x * other.z,
            self.x * other.y - self.y * other.x,
        )

    def norm(self) -> float:
        return math.sqrt(self.dot(self))

    def close(self, other: "Vec3", eps: float = EDGE_EPS) -> bool:
        return (self - other).norm() < eps


def _parse_vec3(s: str) -> Vec3:
    parts = s.strip().split()
    if len(parts) != 3:
        raise ValueError(f"expected 3 components, got {s!r}")
    return Vec3(float(parts[0]), float(parts[1]), float(parts[2]))


def _parse_polyline_2d(text: str) -> list[tuple[float, float]]:
    """Parses 2D polyline text of the form 'x0 y0, x1 y1, x2 y2, ...'."""
    verts: list[tuple[float, float]] = []
    for piece in text.strip().split(","):
        parts = piece.strip().split()
        if len(parts) >= 2:
            verts.append((float(parts[0]), float(parts[1])))
    return verts


def _polygon3d_to_world(elem: ET.Element) -> list[Vec3]:
    """Converts a <Polygon3D offset=... normal=... localX=...>polyline</Polygon3D>
    into a list of world-space 3D vertices."""
    offset = _parse_vec3(elem.attrib.get("offset", "0 0 0"))
    normal = _parse_vec3(elem.attrib.get("normal", "0 0 1"))
    local_x = _parse_vec3(elem.attrib.get("localX", "1 0 0"))
    local_y = normal.cross(local_x)

    verts2d = _parse_polyline_2d(elem.text or "")
    return [offset + local_x * x + local_y * y for x, y in verts2d]


def _iter_surface_polygon3d(surface_elem: ET.Element) -> ET.Element | None:
    """Returns the main Polygon3D element of a Surface (direct child), skipping
    nested ones in holes / sub-surfaces."""
    for child in surface_elem:
        tag = child.tag.rsplit("}", 1)[-1]
        if tag == "Polygon3D":
            return child
    return None


@dataclass
class SurfaceStats:
    name: str
    vertices: list[Vec3]
    sub_surface_count: int
    hole_count: int


@dataclass
class RoomStats:
    room_id: str
    display_name: str
    surface_count: int
    surface_vertex_total: int
    uncovered_edges: int
    closed: bool
    sub_surface_count: int
    hole_count: int


def _collect_surface(surface_elem: ET.Element) -> SurfaceStats:
    poly = _iter_surface_polygon3d(surface_elem)
    vertices: list[Vec3] = _polygon3d_to_world(poly) if poly is not None else []
    name = surface_elem.attrib.get("displayName", "")
    sub_count = 0
    hole_count = 0
    for child in surface_elem.iter():
        tag = child.tag.rsplit("}", 1)[-1]
        if tag == "SubSurface":
            sub_count += 1
        elif tag == "Hole":
            hole_count += 1
    return SurfaceStats(name=name, vertices=vertices,
                        sub_surface_count=sub_count, hole_count=hole_count)


def _uncovered_edge_count(surfaces: list[SurfaceStats]) -> int:
    """Interval-based edge coverage check, mirrors VICUS_Room.cpp.

    For each edge of each surface, compute 1D intervals along the edge of every
    parallel+coincident edge from other surfaces. If those intervals merge to
    cover the whole [0,1] parametrization of the edge (within ABS_TOLERANCE),
    the edge is covered — otherwise uncovered segments contribute to the
    uncovered count. Uncovered segments shorter than MIN_OPEN_EDGE_LENGTH are
    treated as floating-point noise and NOT counted.
    """
    edges: list[tuple[int, Vec3, Vec3]] = []
    for si, s in enumerate(surfaces):
        n = len(s.vertices)
        for i in range(n):
            edges.append((si, s.vertices[i], s.vertices[(i + 1) % n]))

    def on_line(p: Vec3, a: Vec3, d: Vec3, length: float) -> float | None:
        """Project p onto the line through a with direction d (length |d|=length).
        Returns the normalized parameter t ∈ ℝ such that a + t*d is the foot of
        perpendicular from p. Returns None if p is further than EDGE_EPS from
        the infinite line."""
        if length < 1e-10:
            return None
        inv_len = 1.0 / length
        unit = Vec3(d.x * inv_len, d.y * inv_len, d.z * inv_len)
        ap = p - a
        t_meters = ap.dot(unit)          # signed distance along line from a to foot
        foot = a + unit * t_meters
        if (p - foot).norm() > EDGE_EPS:
            return None
        return t_meters * inv_len        # normalized [0..1] on segment (a, a+d)

    uncovered_total = 0
    for i, (owner_i, a, b) in enumerate(edges):
        d = b - a
        length = d.norm()
        if length < 1e-10:
            continue

        # Collect intervals from all other edges of DIFFERENT surfaces that lie
        # on this line (parallel + colinear within tolerance).
        intervals: list[tuple[float, float]] = []
        for j, (owner_j, c, e) in enumerate(edges):
            if j == i or owner_j == owner_i:
                continue
            d2 = e - c
            len2 = d2.norm()
            if len2 < 1e-10:
                continue
            # parallel check via normalized dot product
            dot = abs(d.dot(d2)) / (length * len2)
            if dot > 1.0:
                dot = 1.0
            if dot < math.cos(ANGLE_PARALLEL_TOL_RAD):
                continue  # not parallel
            tc = on_line(c, a, d, length)
            te = on_line(e, a, d, length)
            if tc is None or te is None:
                continue
            lo, hi = sorted((tc, te))
            # clip to [0,1]
            lo = max(0.0, lo)
            hi = min(1.0, hi)
            if hi - lo > 1e-6:
                intervals.append((lo, hi))

        # Merge intervals and find uncovered gaps on [0,1]
        if not intervals:
            # Entire edge uncovered if long enough to count
            if length >= MIN_OPEN_EDGE_LENGTH:
                uncovered_total += 1
            continue

        intervals.sort()
        merged: list[tuple[float, float]] = [intervals[0]]
        for lo, hi in intervals[1:]:
            mlo, mhi = merged[-1]
            if lo <= mhi + (EDGE_EPS / length):
                merged[-1] = (mlo, max(mhi, hi))
            else:
                merged.append((lo, hi))

        # Check for uncovered gaps along the parametric edge
        uncov_here = 0
        prev_end = 0.0
        for lo, hi in merged:
            gap_len = (lo - prev_end) * length
            if gap_len > MIN_OPEN_EDGE_LENGTH:
                uncov_here += 1
            prev_end = max(prev_end, hi)
        tail_len = (1.0 - prev_end) * length
        if tail_len > MIN_OPEN_EDGE_LENGTH:
            uncov_here += 1
        uncovered_total += uncov_here

    return uncovered_total


def analyze_vicus(vicus_path: Path) -> dict:
    # VICUS XML uses <IBK:Parameter ...> without declaring xmlns:IBK, which strict
    # XML parsers reject as "unbound prefix". Inject a dummy namespace declaration
    # before parsing so ElementTree accepts it. We don't actually use the IBK nodes
    # for quality analysis.
    raw = vicus_path.read_text(encoding="utf-8", errors="replace")
    if 'xmlns:IBK=' not in raw:
        raw = raw.replace(
            "<VicusProject ",
            '<VicusProject xmlns:IBK="urn:local:ibk" ',
            1,
        )
    root = ET.fromstring(raw)
    ns = root.tag.split("}", 1)[0][1:] if "}" in root.tag else ""

    def q(name: str) -> str:
        return f"{{{ns}}}{name}" if ns else name

    rooms: list[RoomStats] = []
    total_surfaces = 0
    total_sub_surfaces = 0
    total_holes = 0
    total_rooms_open = 0
    total_uncovered = 0

    # Walk Buildings → BuildingLevels → Rooms → Surfaces
    for room_elem in root.iter(q("Room")):
        room_surfaces: list[SurfaceStats] = []
        surfaces_container = room_elem.find(q("Surfaces"))
        if surfaces_container is not None:
            for s_elem in surfaces_container.findall(q("Surface")):
                room_surfaces.append(_collect_surface(s_elem))
        uncov = _uncovered_edge_count(room_surfaces)
        sub_count = sum(s.sub_surface_count for s in room_surfaces)
        hole_count = sum(s.hole_count for s in room_surfaces)
        vtx_total = sum(len(s.vertices) for s in room_surfaces)
        rs = RoomStats(
            room_id=room_elem.attrib.get("id", ""),
            display_name=room_elem.attrib.get("displayName", ""),
            surface_count=len(room_surfaces),
            surface_vertex_total=vtx_total,
            uncovered_edges=uncov,
            closed=(uncov == 0),
            sub_surface_count=sub_count,
            hole_count=hole_count,
        )
        rooms.append(rs)
        total_surfaces += rs.surface_count
        total_sub_surfaces += sub_count
        total_holes += hole_count
        if not rs.closed:
            total_rooms_open += 1
            total_uncovered += rs.uncovered_edges

    ci_count = sum(1 for _ in root.iter(q("ComponentInstance")))
    ssci_count = sum(1 for _ in root.iter(q("SubSurfaceComponentInstance")))
    shading_count = sum(1 for _ in root.iter(q("ShadingObject")))
    shading_surface_count = 0
    for so in root.iter(q("ShadingObject")):
        for _ in so.iter(q("Surface")):
            shading_surface_count += 1

    return {
        "rooms": rooms,
        "rooms_total": len(rooms),
        "rooms_open": total_rooms_open,
        "rooms_closed": len(rooms) - total_rooms_open,
        "total_surfaces": total_surfaces,
        "total_uncovered_edges": total_uncovered,
        "total_sub_surfaces": total_sub_surfaces,
        "total_holes": total_holes,
        "component_instances": ci_count,
        "subsurface_component_instances": ssci_count,
        "shading_objects": shading_count,
        "shading_surfaces": shading_surface_count,
    }


# ---------- IFC step-file counting ----------
# Matches lines like `#123= IFCFOO(...)` OR `#123 =IFCFOO(...)` OR
# `#123  =  IFCFOO(...)` — the separator between id and entity can be any
# whitespace (or no whitespace) on either side of the `=`.
_IFC_ENTITY_RE = re.compile(r"^\s*#\d+\s*=\s*([A-Z0-9_]+)\s*\(", re.IGNORECASE)


def count_ifc_entities(ifc_path: Path) -> dict[str, int]:
    counts: dict[str, int] = {}
    # STEP files are mostly ASCII; read in binary to avoid locale issues, decode lenient.
    with ifc_path.open("rb") as fh:
        for raw in fh:
            line = raw.decode("utf-8", errors="replace")
            m = _IFC_ENTITY_RE.match(line.lstrip())
            if m:
                entity = m.group(1).upper()
                counts[entity] = counts.get(entity, 0) + 1
    return counts


def print_report(vicus_path: Path, stats: dict, ifc_counts: dict[str, int] | None = None) -> None:
    print(f"=== VICUS quality report: {vicus_path.name} ===")
    print(f"  rooms                         : {stats['rooms_total']}")
    print(f"  rooms closed (no uncov. edges): {stats['rooms_closed']}")
    print(f"  rooms OPEN (with holes)       : {stats['rooms_open']}")
    print(f"  total uncovered edges         : {stats['total_uncovered_edges']}")
    print(f"  total room surfaces           : {stats['total_surfaces']}")
    print(f"  total holes on surfaces       : {stats['total_holes']}")
    print(f"  total sub-surfaces (openings) : {stats['total_sub_surfaces']}")
    print(f"  ComponentInstances            : {stats['component_instances']}")
    print(f"  SubSurfaceComponentInstances  : {stats['subsurface_component_instances']}")
    print(f"  ShadingObjects                : {stats['shading_objects']}")
    print(f"  Shading surfaces              : {stats['shading_surfaces']}")
    if ifc_counts is not None:
        wins = ifc_counts.get("IFCWINDOW", 0)
        doors = ifc_counts.get("IFCDOOR", 0)
        ops = ifc_counts.get("IFCOPENINGELEMENT", 0)
        spaces = ifc_counts.get("IFCSPACE", 0)
        walls = (ifc_counts.get("IFCWALL", 0)
                 + ifc_counts.get("IFCWALLSTANDARDCASE", 0))
        expected_openings = wins + doors
        matched = stats["subsurface_component_instances"]
        rate = (100.0 * matched / expected_openings) if expected_openings > 0 else 0.0
        print(f"  IFC sources:")
        print(f"    IfcSpace                    : {spaces}")
        print(f"    IfcWall/StandardCase        : {walls}")
        print(f"    IfcWindow                   : {wins}")
        print(f"    IfcDoor                     : {doors}")
        print(f"    IfcOpeningElement           : {ops}")
        print(f"  Opening match rate            : {matched}/{expected_openings}  ({rate:.1f}%)")

    print()
    # Room-by-room problems
    open_rooms = [r for r in stats["rooms"] if not r.closed]
    if open_rooms:
        print(f"  Rooms with uncovered edges (top 10 by count):")
        open_rooms.sort(key=lambda r: r.uncovered_edges, reverse=True)
        for r in open_rooms[:10]:
            print(f"    id={r.room_id:>8s}  uncov={r.uncovered_edges:>4d}  "
                  f"surfs={r.surface_count:>3d}  subs={r.sub_surface_count:>3d}  "
                  f"name={r.display_name!r}")


def main(argv: Iterable[str]) -> int:
    parser = argparse.ArgumentParser(description="VICUS import quality analyzer")
    parser.add_argument("--vicus", required=True, type=Path, help="path to .vicus file")
    parser.add_argument("--ifc", type=Path, help="optional source IFC file for cross-ref")
    args = parser.parse_args(list(argv))

    if not args.vicus.exists():
        print(f"error: VICUS file not found: {args.vicus}", file=sys.stderr)
        return 1
    try:
        stats = analyze_vicus(args.vicus)
    except ET.ParseError as e:
        print(f"error: XML parse: {e}", file=sys.stderr)
        return 1

    ifc_counts = None
    if args.ifc is not None and args.ifc.exists():
        ifc_counts = count_ifc_entities(args.ifc)
    elif args.ifc is not None:
        print(f"warning: IFC file not found: {args.ifc}", file=sys.stderr)

    print_report(args.vicus, stats, ifc_counts)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
