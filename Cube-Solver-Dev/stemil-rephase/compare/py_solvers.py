#!/usr/bin/env python3
"""Batch workers for the two Kociemba baselines. Run with <repo>/rephase/.venv/bin/python.

  py_solvers.py kociemba   C two-phase (pip kociemba 1.2.1), first solution
  py_solvers.py hk         hkociemba RubikTwoPhase (symmetry-reduced), first solution, 6 threads

stdin : "<seed>\t<scramble>" per line      stdout: one JSON per line
Table/library initialisation happens in a warm-up solve and is excluded from the time.
Time = wall clock of the single solve call (first solution returned). Memory = process peak RSS.
"""
import json, os, resource, sys, time
from pathlib import Path

REPHASE = Path(os.environ.get("REPHASE_DIR", Path(__file__).resolve().parents[2] / "rephase"))
sys.path.insert(0, str(REPHASE))
from boundary_repair.backends import initial_solution
from boundary_repair.cube import apply_moves, format_moves, parse_moves, to_facelets, transform, IDENTITY

def peak_rss_kib():
    # VmHWM, not ru_maxrss: ru_maxrss is inherited from the (large) parent process across fork/exec.
    for line in open("/proc/self/status"):
        if line.startswith("VmHWM:"):
            return int(line.split()[1])


MAX_LEN = 24  # same cap for both; first solution <= MAX_LEN is returned


def kociemba(state):
    t = time.perf_counter()
    r = initial_solution(state, "kociemba", 60.0, MAX_LEN)
    return r.moves, (time.perf_counter() - t) * 1000  # r.elapsed_ms would also do; verify time excluded either way


def hk_setup():
    import twophase.defs as defs
    defs.FOLDER = str(REPHASE / "twophase_tables")
    import twophase.solver as solver
    names = {"1": "", "2": "2", "3": "'"}

    def solve(state):
        facelets = to_facelets(state)
        t = time.perf_counter()
        out = solver.solve(facelets, MAX_LEN, 0)  # timeout 0: return as soon as the first solution exists
        ms = (time.perf_counter() - t) * 1000
        if not isinstance(out, str):
            raise RuntimeError(f"hk solver error code {out}")
        text = " ".join(tok[0] + names[tok[1]] for tok in out.split("(")[0].split())
        return parse_moves(text), ms
    return solve


def main():
    mode = sys.argv[1]
    solve = kociemba if mode == "kociemba" else hk_setup()
    solve(transform(parse_moves("R U F2 D' L B2 U' R2 F")))  # warm-up (not timed, not reported)
    for line in sys.stdin:
        seed, _, scramble = line.rstrip("\n").partition("\t")
        if not _:
            continue
        state = transform(parse_moves(scramble))
        moves, ms = solve(state)
        ok = apply_moves(state, moves) == IDENTITY
        print(json.dumps({"seed": int(seed), "solved": ok, "htm": len(moves), "ms": round(ms, 3),
                          "peak_rss_kib": peak_rss_kib(),
                          "moves": format_moves(moves)}), flush=True)


if __name__ == "__main__":
    main()
