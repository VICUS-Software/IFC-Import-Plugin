#!/usr/bin/env python3
"""Per-IFC unified quality summary: takes a batch directory of .vicus + .log
files (produced by batch_ifc_test.py) and the source IFC dir, runs the opening
match-quality cross-check on each, prints a sortable table with the
aggregated quality score plus its components.

Usage:
    vicus_quality_summary.py --out /tmp/ifc-batch --ifc-dir /path/to/ifc
"""
from __future__ import annotations
import argparse
import re
import subprocess
import sys
from pathlib import Path

_THIS = Path(__file__).resolve().parent
_TOOL = _THIS / "vicus_opening_match_quality.py"


def parse_quality(text: str) -> dict:
    out = {
        "coverage": None,
        "unmatched": None,
        "wrong_room": None,
        "wrong_room_total": None,
        "missing_parent": None,
        "matched_total": None,
        "p95": None,
        "score": None,
    }
    for line in text.splitlines():
        line = line.strip()
        m = re.match(r"matched.*:\s*(\d+)\s*\(([\d.]+)%\)", line)
        if m:
            out["matched_total"] = int(m.group(1))
            out["coverage"] = float(m.group(2)) / 100.0
        m = re.match(r"unmatched IFC openings\s*:\s*(\d+)", line)
        if m:
            out["unmatched"] = int(m.group(1))
        m = re.match(r"in WRONG room\s*:\s*(\d+)/(\d+)\s*\(([\d.]+)%\)", line)
        if m:
            out["wrong_room"] = int(m.group(1))
            out["wrong_room_total"] = int(m.group(2))
        m = re.match(r"attached to 'Missing' parent:\s*(\d+)/(\d+)", line)
        if m:
            out["missing_parent"] = int(m.group(1))
        m = re.search(r"p95=([\d.]+)", line)
        if m:
            out["p95"] = float(m.group(1))
        m = re.search(r"OVERALL QUALITY SCORE\s*:\s*([\d.]+)", line)
        if m:
            out["score"] = float(m.group(1))
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, type=Path,
                    help="Batch output directory with *.vicus and SUMMARY.txt")
    ap.add_argument("--ifc-dir", required=True, type=Path)
    ap.add_argument("--tol", type=float, default=3.0)
    args = ap.parse_args(argv)

    # Parse import times from SUMMARY.txt of batch
    summary_path = args.out / "SUMMARY.txt"
    times: dict[str, float] = {}
    if summary_path.exists():
        for line in summary_path.read_text(errors="replace").splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[0].endswith(".ifc"):
                try:
                    times[parts[0]] = float(parts[2])
                except ValueError:
                    pass

    rows = []
    for vicus in sorted(args.out.glob("*.vicus")):
        stem = vicus.stem
        ifc = args.ifc_dir / (stem + ".ifc")
        if not ifc.exists():
            ifc = args.ifc_dir / (stem + ".IFC")
        if not ifc.exists():
            continue
        time_s = times.get(stem + ".ifc")
        cmd = [sys.executable, str(_TOOL),
               "--vicus", str(vicus), "--ifc", str(ifc),
               "--tol", str(args.tol)]
        if time_s is not None:
            cmd += ["--time-s", str(time_s)]
        try:
            result = subprocess.run(cmd, capture_output=True, text=True,
                                    timeout=300)
            stats = parse_quality(result.stdout)
            stats["file"] = stem
            stats["time_s"] = time_s
            rows.append(stats)
        except subprocess.TimeoutExpired:
            rows.append({"file": stem, "score": None, "time_s": "timeout"})

    rows.sort(key=lambda r: -(r.get("score") or -1))

    headers = ["score", "file", "cov%", "unmatched", "wrong_room", "missing", "p95(m)", "time_s"]
    widths = [6, 50, 6, 9, 14, 8, 7, 7]
    print("  ".join(h.ljust(w) for h, w in zip(headers, widths)))
    print("  ".join("-" * w for w in widths))
    for r in rows:
        sc = f"{r['score']:.1f}" if r.get("score") is not None else "—"
        cov = f"{(r.get('coverage') or 0) * 100:.0f}" if r.get("coverage") is not None else "—"
        unm = str(r.get("unmatched") or "—")
        wr = f"{r.get('wrong_room','—')}/{r.get('wrong_room_total','—')}"
        miss = f"{r.get('missing_parent') or 0}/{r.get('matched_total') or 0}"
        p95 = f"{r.get('p95'):.2f}" if r.get("p95") is not None else "—"
        ts = f"{r.get('time_s'):.1f}" if isinstance(r.get('time_s'), (int, float)) else str(r.get('time_s') or "—")
        cells = [sc, r["file"][:50], cov, unm, wr, miss, p95, ts]
        print("  ".join(c.ljust(w) for c, w in zip(cells, widths)))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
