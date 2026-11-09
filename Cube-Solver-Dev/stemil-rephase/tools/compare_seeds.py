#!/usr/bin/env python3
"""Same-seed comparison on cubes where rephase shortened the stemil solution.

1. One stemil-rephase process (tables loaded once) solves seeds S..S+N-1 -> JSON lines.
   Each line holds stemil alone (initial_*) and stemil+rephase (htm, elapsed_ms, ...).
2. For every improved seed, the SAME scramble is solved by kociemba two-phase
   (../rephase backend) in a fresh process: wall time + peak RSS + moves.
Output: CSV (per seed) + summary on stdout.
"""
import argparse, csv, json, statistics, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KOCIEMBA_DIR = ROOT.parent / "rephase"  # holds boundary_repair + .venv

WORKER = r"""
import json, resource, sys, time
from boundary_repair import initial_solution, parse_moves, transform, format_moves
state = transform(parse_moves(sys.argv[1]))
base = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
t = time.perf_counter()
r = initial_solution(state, "kociemba", float(sys.argv[2]), int(sys.argv[3]))
ms = (time.perf_counter() - t) * 1000
print(json.dumps({"moves": format_moves(r.moves), "htm": len(r.moves), "ms": ms,
                  "baseline_rss_kib": base,
                  "peak_rss_kib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}))
"""


def kociemba(scramble, timeout, max_length, repeat):
    runs = []
    for _ in range(repeat):  # fresh process each time: cold RSS, no table cache
        out = subprocess.run([str(KOCIEMBA_DIR / ".venv/bin/python"), "-c", WORKER, scramble,
                              str(timeout), str(max_length)], cwd=KOCIEMBA_DIR,
                             capture_output=True, text=True, check=True).stdout
        runs.append(json.loads(out))
    best = runs[0]
    best["ms"] = statistics.median(r["ms"] for r in runs)
    best["peak_rss_kib"] = max(r["peak_rss_kib"] for r in runs)
    return best


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--seed", type=int, default=2026)
    p.add_argument("--count", type=int, default=100, help="seeds scanned: seed..seed+count-1")
    p.add_argument("--scramble-length", type=int, default=25)
    p.add_argument("--budget-ms", type=float, default=1000)
    p.add_argument("--rephase-ms", type=float, default=100)
    p.add_argument("--windows", default="4,6,8,10")
    p.add_argument("--kociemba-timeout", type=float, default=3.0)
    p.add_argument("--kociemba-max-length", type=int, default=24)
    p.add_argument("--kociemba-repeat", type=int, default=3, help="time = median of N fresh processes")
    p.add_argument("--out", type=Path, default=ROOT / "results" / "compare_seeds.csv")
    a = p.parse_args()

    cmd = [str(ROOT / "build/stemil-rephase"), "solve", "--json", "--seed", str(a.seed),
           "--runs", str(a.count), "--scramble-length", str(a.scramble_length),
           "--budget-ms", str(a.budget_ms), "--rephase-ms", str(a.rephase_ms), "--windows", a.windows]
    print("stemil-rephase:", " ".join(cmd), file=sys.stderr)
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True)
    rows = [json.loads(l) for l in proc.stdout.splitlines() if l.startswith("{")]
    improved = [r for r in rows if r["solved"] and r["htm"] < r["initial_htm"]]
    print(f"scanned {len(rows)} seeds, rephase improved {len(improved)}", file=sys.stderr)

    out = []
    for r in improved:
        k = kociemba(r["scramble"], a.kociemba_timeout, a.kociemba_max_length, a.kociemba_repeat)
        out.append({
            "seed": r["seed"], "scramble": r["scramble"],
            "kociemba_htm": k["htm"], "stemil_htm": r["initial_htm"], "rephase_htm": r["htm"],
            "kociemba_ms": round(k["ms"], 3), "stemil_ms": r["two_phase_ms"],
            "rephase_total_ms": r["elapsed_ms"], "rephase_only_ms": r["rephase_ms"],
            "kociemba_rss_mib": round(k["peak_rss_kib"] / 1024, 1),
            "stemil_rss_mib": round(r["stemil_peak_rss_kib"] / 1024, 1),
            "rephase_rss_mib": round(r["peak_rss_kib"] / 1024, 1),
            "rephase_extra_mib": round(r["rephase_peak_bytes"] / 2**20, 2),
            "kociemba_moves": k["moves"], "stemil_moves": r["initial_sequence"],
            "rephase_moves": r["sequence"]})
    if not out:
        sys.exit("no improved seeds; raise --count")
    a.out.parent.mkdir(parents=True, exist_ok=True)
    with a.out.open("w", newline="") as f:
        w = csv.DictWriter(f, out[0].keys()); w.writeheader(); w.writerows(out)

    m = lambda k: statistics.mean(o[k] for o in out)
    print(f"improved seeds: {len(out)}  (CSV: {a.out})")
    print(f"{'':10}{'moves':>8}{'time ms':>10}{'RSS MiB':>10}")
    print(f"{'kociemba':10}{m('kociemba_htm'):8.2f}{m('kociemba_ms'):10.1f}{m('kociemba_rss_mib'):10.1f}")
    print(f"{'stemil':10}{m('stemil_htm'):8.2f}{m('stemil_ms'):10.1f}{m('stemil_rss_mib'):10.1f}")
    print(f"{'rephase':10}{m('rephase_htm'):8.2f}{m('rephase_total_ms'):10.1f}{m('rephase_rss_mib'):10.1f}"
          f"   (rephase only {m('rephase_only_ms'):.1f} ms, +{m('rephase_extra_mib'):.2f} MiB build)")
    print(f"rephase vs stemil moves:   {m('rephase_htm') - m('stemil_htm'):+.2f}")
    print(f"rephase vs kociemba moves: {m('rephase_htm') - m('kociemba_htm'):+.2f}")


if __name__ == "__main__":
    main()
