#!/usr/bin/env python3
"""Batch-imports all IFC files in a directory via IFC2BESTest CLI and runs the
VICUS quality analyzer on each result. Emits a tabular summary and flags
regressions.

Example:
    batch_ifc_test.py \
        --ifc-dir /home/hirth/VICUS-cloud/VICUS-Daten/02_Entwicklung/16_Testdateien/IFC \
        --bin    /mnt/Daten/99-git/VICUS/IFC-Import-Plugin/bin/release/IFC2BESTest \
        --out    /tmp/ifc-batch-results
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

# The quality analyzer lives next to this script.
_THIS_DIR = Path(__file__).resolve().parent
_QUALITY_TOOL = _THIS_DIR / "vicus_quality.py"


def run_import(binary: Path, ifc: Path, vicus_out: Path, log_out: Path,
               timeout_s: int = 900) -> tuple[bool, float]:
    """Runs IFC2BESTest CLI, captures stdout/stderr to log_out, returns
    (success, elapsed_seconds)."""
    t0 = time.monotonic()
    with log_out.open("wb") as lf:
        proc = subprocess.Popen(
            [str(binary), "--input", str(ifc), "--output", str(vicus_out)],
            stdout=lf, stderr=subprocess.STDOUT,
        )
        try:
            rc = proc.wait(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            proc.kill()
            return False, timeout_s
    return rc == 0, time.monotonic() - t0


def run_quality(vicus: Path, ifc: Path | None, report_out: Path) -> bool:
    if not vicus.exists():
        report_out.write_text(f"no vicus file: {vicus}\n")
        return False
    cmd = [sys.executable, str(_QUALITY_TOOL), "--vicus", str(vicus)]
    if ifc is not None:
        cmd += ["--ifc", str(ifc)]
    with report_out.open("wb") as rf:
        rc = subprocess.call(cmd, stdout=rf, stderr=subprocess.STDOUT)
    return rc == 0


def parse_report(report: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    if not report.exists():
        return out
    for line in report.read_text(errors="replace").splitlines():
        if ":" in line and line.startswith("  "):
            k, _, v = line.strip().partition(":")
            out[k.strip()] = v.strip()
    return out


def _process_one(args_tuple):
    """Worker: import + analyze ONE IFC. Pickle-friendly (top-level fn)."""
    binary, ifc, vicus_out, log_out, report_out, timeout, skip_existing = args_tuple
    if skip_existing and vicus_out.exists() and report_out.exists():
        ok_import, elapsed = True, 0.0
    else:
        ok_import, elapsed = run_import(binary, ifc, vicus_out, log_out, timeout)
        run_quality(vicus_out, ifc, report_out)
    return (ifc, ok_import, elapsed)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Batch IFC → VICUS test runner")
    parser.add_argument("--ifc-dir", required=True, type=Path)
    parser.add_argument("--bin", required=True, type=Path,
                        help="path to IFC2BESTest binary")
    parser.add_argument("--out", required=True, type=Path,
                        help="output directory for .vicus, logs, reports")
    parser.add_argument("--pattern", default="*.ifc",
                        help="glob pattern for IFC files (default *.ifc)")
    parser.add_argument("--timeout", type=int, default=900,
                        help="per-file timeout in seconds (default 900)")
    parser.add_argument("--skip-existing", action="store_true",
                        help="skip files whose output .vicus already exists")
    parser.add_argument("--jobs", "-j", type=int, default=1,
                        help="run that many imports in parallel (default 1)")
    parser.add_argument("--skip", default="",
                        help="comma-separated substrings; files matching any are skipped")
    args = parser.parse_args(argv)

    if not args.ifc_dir.is_dir():
        print(f"error: --ifc-dir does not exist: {args.ifc_dir}", file=sys.stderr)
        return 1
    if not args.bin.exists():
        print(f"error: --bin not found: {args.bin}", file=sys.stderr)
        return 1
    args.out.mkdir(parents=True, exist_ok=True)

    ifc_files = sorted(args.ifc_dir.glob(args.pattern))
    if args.skip:
        skip_terms = [s.strip() for s in args.skip.split(",") if s.strip()]
        ifc_files = [p for p in ifc_files if not any(t in p.name for t in skip_terms)]
    if not ifc_files:
        print(f"no IFC files matched {args.pattern} in {args.ifc_dir}", file=sys.stderr)
        return 1

    job_args: list[tuple] = []
    for ifc in ifc_files:
        stem = ifc.stem
        job_args.append((args.bin, ifc,
                         args.out / f"{stem}.vicus",
                         args.out / f"{stem}.log",
                         args.out / f"{stem}.report.txt",
                         args.timeout,
                         args.skip_existing))

    results: dict = {}
    if args.jobs <= 1:
        for ja in job_args:
            print(f"[start] {ja[1].name}", flush=True)
            ifc, ok, elapsed = _process_one(ja)
            print(f"[{'OK ' if ok else 'FAIL'}] {ifc.name}  ({elapsed:.1f}s)", flush=True)
            results[ifc] = (ok, elapsed)
    else:
        # ProcessPool — each worker spawns its own IFC2BESTest. No shared mutable
        # state in the pipeline (every job writes its own files), so the parallel
        # imports never collide.
        from concurrent.futures import ProcessPoolExecutor, as_completed
        print(f"running {len(job_args)} imports with {args.jobs} parallel workers", flush=True)
        for ja in job_args:
            print(f"[queued] {ja[1].name}", flush=True)
        with ProcessPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(_process_one, ja): ja for ja in job_args}
            for fut in as_completed(futures):
                ja = futures[fut]
                try:
                    ifc, ok, elapsed = fut.result()
                    print(f"[{'OK ' if ok else 'FAIL'}] {ifc.name}  ({elapsed:.1f}s)", flush=True)
                    results[ifc] = (ok, elapsed)
                except Exception as e:
                    print(f"[CRASH] {ja[1].name}: {e}", flush=True)
                    results[ja[1]] = (False, 0.0)

    summary_rows: list[dict[str, str]] = []
    for ifc in ifc_files:
        report_out = args.out / f"{ifc.stem}.report.txt"
        ok_import, elapsed = results.get(ifc, (False, 0.0))
        r = parse_report(report_out)
        summary_rows.append({
            "file":      ifc.name,
            "ok":        "1" if ok_import else "0",
            "time_s":    f"{elapsed:.1f}",
            "rooms":     r.get("rooms", ""),
            "open":      r.get("rooms OPEN (with holes)", ""),
            "uncov":     r.get("total uncovered edges", ""),
            "surfs":     r.get("total room surfaces", ""),
            "subs":      r.get("total sub-surfaces (openings)", ""),
            "holes":     r.get("total holes on surfaces", ""),
            "shading":   r.get("ShadingObjects", ""),
            "match":     r.get("Opening match rate", ""),
        })

    # Summary
    summary_txt = args.out / "SUMMARY.txt"
    with summary_txt.open("w") as f:
        headers = ["file", "ok", "time_s", "rooms", "open", "uncov",
                   "surfs", "subs", "holes", "shading", "match"]
        widths = [max(len(h), max((len(r[h]) for r in summary_rows), default=0))
                  for h in headers]
        sep = "  "
        f.write(sep.join(h.ljust(widths[i]) for i, h in enumerate(headers)) + "\n")
        f.write(sep.join("-" * widths[i] for i in range(len(headers))) + "\n")
        for r in summary_rows:
            f.write(sep.join(r[h].ljust(widths[i]) for i, h in enumerate(headers)) + "\n")
    print(f"\nsummary written to {summary_txt}")
    print(summary_txt.read_text())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
