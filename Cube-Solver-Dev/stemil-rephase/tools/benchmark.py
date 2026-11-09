#!/usr/bin/env python3
"""Paired warmed-budget experiment; independent verification and separate processes."""
import argparse
import hashlib
import json
import platform
import random
import statistics
import subprocess
import time
from pathlib import Path

from stickers import SOLVED, apply, uniform_state, shallow_distances

ROOT = Path(__file__).resolve().parents[1]


def mean(items):
    return statistics.mean(items) if items else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--budget-ms", type=float, default=1000)
    parser.add_argument("--seed", type=int, default=20260917)
    parser.add_argument("--fractions", default="0,0.05,0.1,0.2")
    parser.add_argument("--kind", choices=("uniform", "shallow"), default="uniform")
    parser.add_argument("--out", type=Path, default=ROOT / "results" / "uniform")
    parser.add_argument("--input", type=Path)
    parser.add_argument("--resources", type=Path, help="read-only existing stemil resources")
    parser.add_argument("--adaptive", action="store_true")
    parser.add_argument("--windows", default="4,6,8,10")
    parser.add_argument("--repair-modes", default="boundary+sliding", help="comma-separated boundary, boundary+sliding, sliding")
    parser.add_argument("--rephase-ms", type=float)
    parser.add_argument("--boundary-fraction", type=float, default=0.5)
    parser.add_argument("--table-depth", type=int, default=4)
    parser.add_argument("--repair-memory-mib", type=float, default=64)
    args = parser.parse_args()
    if args.count <= 0 or not 0 < args.budget_ms <= 60000:
        parser.error("positive count and budget <=60000 ms required")
    modes=args.repair_modes.split(",")
    if not modes or len(set(modes))!=len(modes) or any(m not in ("boundary","boundary+sliding","sliding") for m in modes):
        parser.error("distinct valid repair modes required")
    if args.rephase_ms is not None:
        if not 0 <= args.rephase_ms < args.budget_ms or args.adaptive:
            parser.error("fixed rephase cap must be >=0 and < total; cannot be adaptive")
        fractions=[0,args.rephase_ms/args.budget_ms] if args.rephase_ms else [0]
    else:
        fractions = [float(s) for s in args.fractions.split(",")]
    if not fractions or fractions[0] != 0 or len(set(fractions)) != len(fractions) or any(not 0 <= f < 1 for f in fractions):
        parser.error("fractions must start with baseline 0, be distinct, and lie in [0,1)")
    rng = random.Random(args.seed)
    if args.input:
        corpus = [json.loads(s) for s in args.input.read_text().splitlines() if s.strip()]
    elif args.kind == "uniform":
        corpus = [{"index": i, "facelets": uniform_state(rng), "optimal_htm": None}
                  for i in range(args.count)]
    else:
        distances = shallow_distances()
        choices = rng.sample(list(distances), min(args.count, len(distances)))
        corpus = [{"index": i, "facelets": s, "optimal_htm": distances[s]}
                  for i, s in enumerate(choices)]
    if not corpus:
        parser.error("empty corpus")
    args.out.mkdir(parents=True, exist_ok=True)
    dataset = "".join(json.dumps(item, sort_keys=True) + "\n" for item in corpus)
    (args.out / "corpus.jsonl").write_text(dataset)
    # Existing ASCII table load time is an explicit setup measurement, outside experiments.
    table_flags = ["--resources", str(args.resources.resolve())] if args.resources else []
    warm = subprocess.run([str(ROOT / "build/stemil-rephase"), "solve", "--json", "--scramble", "",
                           "--repair-fraction", "0", *table_flags], cwd=ROOT, capture_output=True, text=True, timeout=300, check=True)
    warm_info = json.loads(warm.stdout)
    input_text = "".join(item["facelets"] + "\n" for item in corpus)
    runs = {}
    experiments=[(fraction,mode) for fraction in fractions for mode in (modes if fraction else [modes[0]])]
    for fraction,mode in experiments:
        label = f"repair-{fraction:g}" + ("-adaptive" if args.adaptive and fraction else "")
        if len(modes)>1 and fraction:
            label += "-"+mode
        command = [str(ROOT / "build/stemil-rephase"), "batch", "--json", "--budget-ms", str(args.budget_ms),
                   "--repair-fraction", str(fraction), "--table-depth", str(args.table_depth),
                   "--repair-memory-mib", str(args.repair_memory_mib), "--windows", args.windows,
                   "--repair-mode", mode, "--boundary-fraction",str(args.boundary_fraction)]
        if args.rephase_ms is not None and fraction:
            position=command.index("--repair-fraction");del command[position:position+2]
            command += ["--rephase-ms",str(args.rephase_ms)]
        command += table_flags
        if args.adaptive and fraction:
            command += ["--adaptive"]
        started = time.monotonic()
        completed = subprocess.run(command, cwd=ROOT, input=input_text, capture_output=True, text=True,
                                   timeout=300 + len(corpus) * args.budget_ms / 1000 * 1.5)
        (args.out / f"{label}.jsonl").write_text(completed.stdout)
        (args.out / f"{label}.stderr.txt").write_text(completed.stderr)
        if completed.returncode not in (0, 2):
            raise RuntimeError(f"{label} failed: {completed.stderr}")
        rows = [json.loads(s) for s in completed.stdout.splitlines()]
        if len(rows) != len(corpus):
            raise RuntimeError("benchmark output count mismatch")
        for item, row in zip(corpus, rows):
            if row["tables"] != "expanded":
                raise RuntimeError("benchmark requires existing full stemil pruning tables")
            if row["facelets"] != item["facelets"]:
                raise RuntimeError("benchmark input/output ordering mismatch")
            if row["solved"]:
                if apply(item["facelets"], row["sequence"]) != SOLVED:
                    raise RuntimeError("independent sticker verification failed")
                if apply(item["facelets"], row["initial_sequence"]) != SOLVED:
                    raise RuntimeError("independent initial solution verification failed")
                if row["htm"] > row["initial_htm"]:
                    raise RuntimeError("repair increased HTM")
                if item["optimal_htm"] is not None and row["htm"] < item["optimal_htm"]:
                    raise RuntimeError("answer contradicts independent BFS distance")
        runs[label] = rows
        successful = [r for r in rows if r["solved"]]
        print(f"{label}: solved {len(successful)}/{len(rows)}, mean HTM {mean([r['htm'] for r in successful])}, "
              f"local improvements {sum(r['htm'] < r['initial_htm'] for r in successful)}, "
              f"wall {time.monotonic()-started:.1f}s", flush=True)
    baseline = runs[next(iter(runs))]
    summaries = {}
    for label, rows in runs.items():
        successes = [r for r in rows if r["solved"]]
        paired = [(a, b) for a, b in zip(baseline, rows) if a["solved"] and b["solved"]]
        optimal = [(i, r) for i, r in zip(corpus, rows) if i["optimal_htm"] is not None]
        summaries[label] = {
            "solved": len(successes), "total": len(rows),
            "mean_htm_solved": mean([r["htm"] for r in successes]),
            "min_htm": min((r["htm"] for r in successes), default=None),
            "max_htm": max((r["htm"] for r in successes), default=None),
            "mean_initial_htm": mean([r["initial_htm"] for r in successes]),
            "local_improved_cases": sum(r["htm"] < r["initial_htm"] for r in successes),
            "local_moves_saved": sum(r["initial_htm"] - r["htm"] for r in successes),
            "paired_comparable_cases": len(paired),
            "paired_mean_htm_difference": mean([b["htm"] - a["htm"] for a, b in paired]),
            "paired_better": sum(b["htm"] < a["htm"] for a, b in paired),
            "paired_equal": sum(b["htm"] == a["htm"] for a, b in paired),
            "paired_worse": sum(b["htm"] > a["htm"] for a, b in paired),
            "mean_elapsed_ms": mean([r["elapsed_ms"] for r in rows]),
            "max_elapsed_ms": max(r["elapsed_ms"] for r in rows),
            "over_budget_plus_2ms": sum(r["elapsed_ms"] > args.budget_ms + 2 for r in rows),
            "mean_repair_ms": mean([r["repair_ms"] for r in rows]),
            "peak_process_rss_kib": max(r["peak_rss_kib"] for r in rows),
            "repair_table_bytes": max(r["repair_table_bytes"] for r in rows),
            "repair_build_peak_bytes": max(r["repair_build_peak_bytes"] for r in rows),
            "setup_ms": rows[0]["setup_ms"],
            "repair_mode":rows[0]["repair_mode"],"windows":rows[0]["windows"],
            "boundary_improvements":sum(r["boundary_repair"]["improvements"] for r in rows),
            "sliding_improvements":sum(r["sliding_repair"]["improvements"] for r in rows),
            "boundary_moves_saved":sum(r["boundary_repair"]["moves_saved"] for r in rows),
            "sliding_moves_saved":sum(r["sliding_repair"]["moves_saved"] for r in rows),
            "optimal_hit_rate": mean([int(r["solved"] and r["htm"] == i["optimal_htm"]) for i, r in optimal]),
            "independent_verification": "54-sticker integer-rotation model; all reported solutions checked",
        }
    summary = {"kind": args.kind if not args.input else "provided-corpus", "seed": args.seed,
               "corpus_sha256": hashlib.sha256(dataset.encode()).hexdigest(),
               "platform": platform.platform(), "budget_ms": args.budget_ms,
               "table_mode": "expanded", "stemil_resources": warm_info["stemil_resources"],
               "repair_table_depth": args.table_depth,"rephase_ms":args.rephase_ms,"repair_modes":modes,
               "warmup": warm_info, "modes": summaries}
    (args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"Verified results saved to {args.out}", flush=True)


if __name__ == "__main__":
    main()
