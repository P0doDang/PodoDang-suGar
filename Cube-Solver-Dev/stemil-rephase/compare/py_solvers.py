#!/usr/bin/env python3
"""Batch workers for the two Kociemba baselines. Run with <repo>/rephase/.venv/bin/python.

  py_solvers.py kociemba   C two-phase (pip kociemba 1.2.1), first solution
  py_solvers.py hk         hkociemba RubikTwoPhase (symmetry-reduced), first solution, 6 threads
  py_solvers.py rephase [WINDOW MS]   C two-phase first solution + boundary/sliding rephase (default window 8, <=100 ms), 1 thread

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


def rephase(state):
    """Kociemba first solution, then exact repair. ms = base + repair (wall); found_ms = base + repair time until the
    last replacement (hindsight, same rule as the other solvers). Engine preparation is excluded (done in warm-up)."""
    from boundary_repair.repair import RepairConfig, repair_solution
    t = time.perf_counter()
    base = initial_solution(state, "kociemba", 60.0, MAX_LEN)
    base_ms = (time.perf_counter() - t) * 1000
    res = repair_solution(base.moves, base.boundary, RepairConfig((REPHASE_WINDOW,), REPHASE_MS, None, (0,), True), state)
    last = max((i + 1 for i, a in enumerate(res.attempts) if a.replacement is not None), default=0)
    found = base_ms + sum(a.elapsed_ms for a in res.attempts[:last])
    return res.moves, base_ms + res.elapsed_ms, dict(initial_htm=len(base.moves), found_ms=round(found, 3),
                                                    base_ms=round(base_ms, 3), repair_ms=round(res.elapsed_ms, 3))


REPHASE_WINDOW, REPHASE_MS = 8, 100.0


def main():
    global REPHASE_WINDOW, REPHASE_MS
    mode = sys.argv[1]
    if mode == "rephase" and len(sys.argv) == 4:
        REPHASE_WINDOW, REPHASE_MS = int(sys.argv[2]), float(sys.argv[3])
    solve = {"kociemba": kociemba, "rephase": rephase}.get(mode) or hk_setup()
    solve(transform(parse_moves("R U F2 D' L B2 U' R2 F")))  # warm-up (not timed, not reported)
    for line in sys.stdin:
        seed, _, scramble = line.rstrip("\n").partition("\t")
        if not _:
            continue
        state = transform(parse_moves(scramble))
        out = solve(state)
        moves, ms = out[0], out[1]
        extra = out[2] if len(out) > 2 else {}
        ok = apply_moves(state, moves) == IDENTITY
        print(json.dumps({"seed": int(seed), "scramble": scramble, "solved": ok, "htm": len(moves), "ms": round(ms, 3),
                          "peak_rss_kib": peak_rss_kib(),
                          "moves": format_moves(moves), **extra}), flush=True)


if __name__ == "__main__":
    main()
