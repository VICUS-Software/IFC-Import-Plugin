#!/usr/bin/env python3
"""Re-runs the VICUS quality analyzer on existing .vicus outputs in a batch
directory, WITHOUT re-running the IFC imports. Regenerates .report.txt and
SUMMARY.txt. Used when only the analyzer itself changed.
"""
from __future__ import annotations
import argparse, subprocess, sys
from pathlib import Path

_THIS_DIR = Path(__file__).resolve().parent
_TOOL = _THIS_DIR / "vicus_quality.py"


def parse_report(p: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    if not p.exists(): return out
    for line in p.read_text(errors="replace").splitlines():
        if ":" in line and line.startswith("  "):
            k, _, v = line.strip().partition(":")
            out[k.strip()] = v.strip()
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--ifc-dir", required=True, type=Path)
    args = ap.parse_args(argv)

    rows = []
    for vicus in sorted(args.out.glob("*.vicus")):
        stem = vicus.stem
        ifc = args.ifc_dir / (stem + ".ifc")
        report = args.out / f"{stem}.report.txt"
        cmd = [sys.executable, str(_TOOL), "--vicus", str(vicus)]
        if ifc.exists():
            cmd += ["--ifc", str(ifc)]
        with report.open("wb") as rf:
            subprocess.call(cmd, stdout=rf, stderr=subprocess.STDOUT)
        r = parse_report(report)
        rows.append({
            "file":    stem + ".ifc",
            "rooms":   r.get("rooms", ""),
            "open":    r.get("rooms OPEN (with holes)", ""),
            "uncov":   r.get("total uncovered edges", ""),
            "surfs":   r.get("total room surfaces", ""),
            "subs":    r.get("total sub-surfaces (openings)", ""),
            "shading": r.get("ShadingObjects", ""),
            "match":   r.get("Opening match rate", ""),
        })
        print(f"  {stem}: open={r.get('rooms OPEN (with holes)', '?')}/{r.get('rooms', '?')}  match={r.get('Opening match rate', '?')}")

    summary = args.out / "SUMMARY.txt"
    headers = ["file", "rooms", "open", "uncov", "surfs", "subs", "shading", "match"]
    widths  = [max(len(h), max((len(r[h]) for r in rows), default=0)) for h in headers]
    with summary.open("w") as f:
        f.write("  ".join(h.ljust(widths[i]) for i, h in enumerate(headers)) + "\n")
        f.write("  ".join("-" * widths[i] for i in range(len(headers))) + "\n")
        for r in rows:
            f.write("  ".join(r[h].ljust(widths[i]) for i, h in enumerate(headers)) + "\n")
    print(f"\nsummary → {summary}")
    print(summary.read_text())


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
