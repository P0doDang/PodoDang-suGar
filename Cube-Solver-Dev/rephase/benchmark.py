#!/usr/bin/env python3
"""Paired baseline/repair measurements on seeded random-walk scrambles.

These are NOT optimal-solution hit-rate measurements: no global optimal oracle
is used. Every output is checked by the cubie simulator in repair_solution.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import random
import statistics

from boundary_repair import (RepairConfig, format_moves, initial_solution,
                             prepare_engine, repair_solution, transform)
from boundary_repair.scramble import random_scramble


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--count", type=int, default=100)
    p.add_argument("--seed", type=int, default=2026)
    p.add_argument("--scramble-length", type=int, default=20)
    p.add_argument("--window", "--windows", type=int, nargs="+", default=[6, 8])
    p.add_argument("--time-ms", type=float, default=100)
    p.add_argument("--shifts", type=int, nargs="+", default=[0])
    p.add_argument("--sliding", action="store_true")
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    if not 1 <= args.count <= 10_000 or not 1 <= args.scramble_length <= 100:
        p.error("count는 1~10000, scramble-length는 1~100이어야 합니다")
    config = RepairConfig(tuple(args.window), args.time_ms, shifts=tuple(args.shifts),
                          sliding=args.sliding)
    rng = random.Random(args.seed)
    preparation = prepare_engine()
    rows = []
    for i in range(args.count):
        scramble = random_scramble(args.scramble_length, rng=rng)
        state = transform(scramble)
        initial = initial_solution(state)
        result = repair_solution(initial.moves, initial.boundary, config, state)
        rows.append({"index": i, "scramble": format_moves(scramble),
                     "initial_solution": format_moves(initial.moves),
                     "boundary": initial.boundary, "solution": format_moves(result.moves),
                     "initial_length": len(initial.moves), "final_length": len(result.moves),
                     "saved_moves": result.saved_moves, "base_ms": initial.elapsed_ms,
                     "repair_ms": result.elapsed_ms, "verified": result.verified,
                     "stop_reason": result.stop_reason,
                     "search_statuses": dict(Counter(a.status for a in result.attempts))})
    summary = {
        "sample_type": "seeded random-walk scrambles (not uniform random states)",
        "seed": args.seed, "count": args.count, "scramble_length": args.scramble_length,
        "windows": args.window, "time_limit_ms": args.time_ms,
        "shifts": args.shifts, "sliding": args.sliding,
        "engine_prepare_ms": preparation * 1000,
        "average_initial_length": statistics.mean(r["initial_length"] for r in rows),
        "average_final_length": statistics.mean(r["final_length"] for r in rows),
        "improved_cubes": sum(r["saved_moves"] > 0 for r in rows),
        "total_saved_moves": sum(r["saved_moves"] for r in rows),
        "average_base_ms": statistics.mean(r["base_ms"] for r in rows),
        "average_repair_ms": statistics.mean(r["repair_ms"] for r in rows),
        "max_repair_ms": max(r["repair_ms"] for r in rows),
        "all_verified": all(r["verified"] for r in rows),
        "global_optimal_hit_rate": None,
        "rows": rows,
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in summary.items() if k != "rows"}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
