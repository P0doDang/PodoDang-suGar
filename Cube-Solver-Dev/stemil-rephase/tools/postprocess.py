#!/usr/bin/env python3
"""Isolate local repair by reusing recorded baseline sequences unchanged."""
import argparse
import json
import statistics
import subprocess
from pathlib import Path

from stickers import SOLVED, apply

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--budget-ms", type=float, default=100)
    parser.add_argument("--window-slice-ms", type=float, default=8)
    parser.add_argument("--table-depth", type=int, default=4)
    parser.add_argument("--repair-memory-mib", type=float, default=64)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--windows",default="4,6,8,10")
    parser.add_argument("--repair-mode",choices=("boundary","boundary+sliding","sliding"),default="boundary+sliding")
    args = parser.parse_args()
    baseline = [json.loads(s) for s in args.baseline.read_text().splitlines() if s.strip()]
    args.out.mkdir(parents=True, exist_ok=True)
    rows = []
    for i, original in enumerate(baseline):
        if not original["solved"]:
            continue
        command = [str(ROOT / "build/stemil-rephase"), "repair", "--sequence", original["sequence"],
                   "--boundary", str(original["initial_boundary"]), "--budget-ms", str(args.budget_ms),
                   "--window-slice-ms", str(args.window_slice_ms), "--table-depth", str(args.table_depth),
                   "--repair-memory-mib", str(args.repair_memory_mib),"--windows",args.windows,"--repair-mode",args.repair_mode]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=True, timeout=90)
        row = json.loads(result.stdout)
        if not row["equivalent"] or row["htm"] > original["htm"]:
            raise RuntimeError("repair violated transform/length invariant")
        if apply(original["facelets"], row["sequence"]) != SOLVED:
            raise RuntimeError("independent sticker verification failed")
        row.update(index=original["index"], facelets=original["facelets"])
        rows.append(row)
        if (i + 1) % 25 == 0:
            print(f"Postprocessed {i+1}/{len(baseline)}", flush=True)
    (args.out / "rows.jsonl").write_text("".join(json.dumps(row) + "\n" for row in rows))
    summary = {"cases": len(rows), "baseline": str(args.baseline), "repair_budget_ms": args.budget_ms,
               "window_slice_ms": args.window_slice_ms,"repair_mode":args.repair_mode,"windows":args.windows,
               "boundary_improvements":sum(r["boundary_repair"]["improvements"] for r in rows),
               "sliding_improvements":sum(r["sliding_repair"]["improvements"] for r in rows),
               "table_depth": args.table_depth,
               "table_bytes": max(row["repair_table_bytes"] for row in rows),
               "build_peak_bytes": max(row["repair_build_peak_bytes"] for row in rows),
               "mean_before_htm": statistics.mean(row["initial_htm"] for row in rows),
               "mean_after_htm": statistics.mean(row["htm"] for row in rows),
               "improved_cases": sum(row["htm"] < row["initial_htm"] for row in rows),
               "moves_saved": sum(row["initial_htm"] - row["htm"] for row in rows),
               "mean_repair_ms": statistics.mean(row["elapsed_ms"] for row in rows),
               "max_repair_ms": max(row["elapsed_ms"] for row in rows),
               "independent_verification": "all postprocessed solutions checked with 54-sticker rotations"}
    (args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == "__main__":
    main()
