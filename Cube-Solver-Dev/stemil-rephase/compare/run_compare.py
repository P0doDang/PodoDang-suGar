#!/usr/bin/env python3
"""Same-seed comparison: stemil_rephase_6way vs original stemil, kociemba two-phase,
hkociemba two-phase (symmetry), optimal (IDA*), on the seeds where rephase succeeded.

  python3 compare/run_compare.py scan     # 6way over seeds; keep cases where rephase shortened the solution
  python3 compare/run_compare.py solve    # run the four other solvers on exactly those scrambles
  python3 compare/run_compare.py report   # join -> compare.csv, compare_long.csv, summary.txt
  python3 compare/run_compare.py all

Fixed conditions: scramble length 30, 6way = 6 orientations, 1 candidate, phase1 ida, window 8,
Two-Phase 500 ms + rephase <=100 ms; original stemil 500 ms; optimal cap 60 s, 6 threads.
Tables are loaded before timing in every solver. Memory = process peak RSS (tables included).
Solvers run one after another, never concurrently. Every stage is resumable.

Two time columns per algorithm:
  time_to_final_ms  when the FINAL answer first existed, measured in hindsight, same rule for everyone
                    (6way = Two-Phase best found + rephase improvement offset; stemil = last improvement;
                    kociemba/sym/optimal = the single call)
  wall_ms           real time the run took (6way and stemil use their whole budget)
Log: out/run.log gets one timestamped line per event; raw tool output goes to out/*.stderr.
"""
import argparse, csv, json, os, statistics, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent                      # 6way/
OUT = HERE / "out"
REPHASE_DIR = Path(os.environ.get("REPHASE_DIR", ROOT.parent / "rephase"))                 # <repo>/rephase (has .venv + hkociemba tables)
PY = REPHASE_DIR / ".venv/bin/python"
RES = Path(os.environ.get("STEMIL_RESOURCES", ROOT.parent / "third_party/stemil/resources"))  # stemil pruning tables (generated, not in git)
ALGOS = ("stemil", "kociemba", "hk_symmetry", "optimal")
LABEL = {"6way": "6way+rephase", "stemil": "stemil", "kociemba": "kociemba", "hk_symmetry": "kociemba_sym", "optimal": "optimal"}


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def jsonl(path):
    p = Path(path)
    return [json.loads(l) for l in p.read_text().splitlines() if l.startswith("{")] if p.exists() else []


def stream(cmd, stdin=None, out=None, err=None, on_line=None):
    """Run cmd; JSON stdout lines -> out file (+ on_line callback), everything else and stderr -> err file."""
    cmd = [str(c) for c in cmd]
    errf = open(err, "w") if err else subprocess.DEVNULL
    p = subprocess.Popen(cmd, cwd=ROOT, stdin=open(stdin) if stdin else subprocess.DEVNULL,
                         stdout=subprocess.PIPE, stderr=errf, text=True)
    with open(out, "w") as o:
        for line in p.stdout:
            if line.startswith("{"):
                o.write(line); o.flush()
                if on_line:
                    on_line(json.loads(line))
            elif err:
                errf.write(line); errf.flush()
    rc = p.wait()
    if rc:
        tail = Path(err).read_text().splitlines()[-15:] if err else []
        sys.exit(f"[실패] exit {rc}: {' '.join(cmd[:3])} ...\n" + "\n".join(tail))


def is_win(r):
    return r["solved"] and r["htm"] < r["initial_htm"]


# ---------------------------------------------------------------- scan
def scan(a):
    OUT.mkdir(exist_ok=True)
    found = {}
    for f in sorted(OUT.glob("scan_*.jsonl")):
        found.update({r["seed"]: r for r in jsonl(f)})
    wins_upto = lambda end: sum(1 for s, r in found.items() if s < end and is_win(r))
    log(f"scan 시작: seed {a.seed}~{a.seed + a.count - 1}, 목표 rephase 성공 {a.max_cases}건, chunk {a.chunk}개 "
        f"(기존 결과 재사용: {len(found)}개 큐브, 성공 {wins_upto(10**12)}건)")
    for start in range(a.seed, a.seed + a.count, a.chunk):
        n = min(a.chunk, a.seed + a.count - start)
        f = OUT / f"scan_{start}.jsonl"
        if len(jsonl(f)) != n:  # missing or partial chunk: redo
            t = time.time()
            cmd = [ROOT / "build/stemil-rephase", "solve", "--json", "--seed", start, "--runs", n,
                   "--scramble-length", a.scramble_length, "--orientations", 6, "--candidates", 1, "--phase1-mode", "ida",
                   "--resources", RES, "--budget-ms", a.two_phase_ms + a.rephase_ms, "--rephase-ms", a.rephase_ms, "--windows", a.window]
            stream(cmd, out=f, err=OUT / f"scan_{start}.stderr")
            recs = jsonl(f)
            if len(recs) != n:
                sys.exit(f"[실패] seed {start}~{start + n - 1}: 결과 {len(recs)}/{n}건. {OUT / f'scan_{start}.stderr'} 확인")
            found.update({r["seed"]: r for r in recs})
            for r in recs:
                if is_win(r):
                    log(f"  성공 seed {r['seed']}: {r['initial_htm']}수 -> {r['htm']}수 (-{r['initial_htm'] - r['htm']})")
            log(f"scan seed {start}~{start + n - 1} 완료 ({time.time() - t:.0f}s) | 누적 성공 {wins_upto(start + n)}/{a.max_cases}")
        if wins_upto(start + n) >= a.max_cases:
            break
    wins = [r for s, r in sorted(found.items()) if is_win(r)][: a.max_cases]
    (OUT / "cases.tsv").write_text("".join(f"{r['seed']}\t{r['scramble']}\n" for r in wins))
    (OUT / "6way.jsonl").write_text("".join(json.dumps(r) + "\n" for r in wins))
    log(f"scan 완료: 성공 case {len(wins)}건 -> {OUT / 'cases.tsv'}")
    if len(wins) < a.max_cases:
        log(f"  (목표 {a.max_cases}건 미달: --count를 늘리고 다시 실행하면 이어서 진행)")


# ---------------------------------------------------------------- solve
def describe(name, r):
    if name == "optimal":
        if r["htm"] is None:
            return f"미확정 (하한 {r['lower_bound']}수, {r['ms'] / 1000:.1f}s)"
        return f"최적 {r['htm']}수 ({r['ms'] / 1000:.2f}s)"
    if name == "stemil":
        return f"{r['htm']}수 (최종 해 {r['found_ms']:.1f}ms, 전체 {r['total_ms']:.0f}ms)"
    return f"{r['htm']}수 ({r['ms']:.1f}ms)"


def solve(a):
    cases = OUT / "cases.tsv"
    if not cases.exists():
        sys.exit("cases.tsv가 없습니다. 먼저 scan을 실행하세요.")
    n = len(cases.read_text().splitlines())
    jobs = {
        "stemil": [ROOT / "build/stemil-orig", "--time-limit-ms", a.two_phase_ms, "--stemil-dir", RES.parent],
        "kociemba": [PY, "-I", HERE / "py_solvers.py", "kociemba"],
        "hk_symmetry": [PY, "-I", HERE / "py_solvers.py", "hk"],
        "optimal": [ROOT / "build/optimal-batch", "--cap-sec", a.optimal_cap, "--threads", 6, "--resources", RES],
    }
    for name, cmd in jobs.items():
        f = OUT / f"{name}.jsonl"
        if len(jsonl(f)) == n:
            log(f"{name}: 기존 결과 {n}건 재사용")
            continue
        log(f"{name} 시작 (case {n}건). 테이블 로딩 때문에 첫 결과까지 오래 걸릴 수 있음"
            + (f", case당 최대 {a.optimal_cap:.0f}s" if name == "optimal" else ""))
        t, count = time.time(), [0]

        def on(r, name=name):
            count[0] += 1
            log(f"  {name} {count[0]}/{n} seed {r['seed']}: {describe(name, r)}")
        stream(cmd, stdin=cases, out=f, err=OUT / f"{name}.stderr", on_line=on)
        log(f"{name} 완료 ({time.time() - t:.0f}s)")


# ---------------------------------------------------------------- report
def metrics(name, r, w):
    """-> dict(htm, final_ms, wall_ms, mib, moves, ok) for one algorithm on one case."""
    if name == "6way":
        off = w.get("rephase_found_ms")
        approx = off is None  # scan chunk written before the field existed
        if approx:
            off = max(0.0, w["final_found_ms"] - w["two_phase_ms"])
        return dict(htm=w["htm"], final_ms=w["initial_found_ms"] + off, wall_ms=w["elapsed_ms"],
                    mib=w["peak_rss_kib"] / 1024, moves=w["sequence"], ok=True, approx=approx)
    if r is None:
        return dict(htm=None, final_ms=None, wall_ms=None, mib=None, moves=None, ok=False, approx=False)
    ok = r["solved"] and r.get("htm") is not None
    if name == "stemil":
        final, wall = r["found_ms"], r["total_ms"]
    else:
        final, wall = (r["ms"] if ok else None), r["ms"]
    return dict(htm=r["htm"] if ok else None, final_ms=final, wall_ms=wall, mib=r["peak_rss_kib"] / 1024,
                moves=r.get("moves") if ok else None, ok=ok, approx=False)


def report(a):
    if not (OUT / "cases.tsv").exists():
        sys.exit("cases.tsv가 없습니다. 먼저 scan을 실행하세요.")
    cases = [l.split("\t") for l in (OUT / "cases.tsv").read_text().splitlines()]
    if not cases:
        sys.exit("rephase 성공 case가 0건입니다. scan 범위/조건을 확인하세요.")
    six = {r["seed"]: r for r in jsonl(OUT / "6way.jsonl")}
    data = {n: {r["seed"]: r for r in jsonl(OUT / f"{n}.jsonl")} for n in ALGOS}
    rows, long_rows, approx = [], [], 0
    for seed_s, scramble in cases:
        seed, w = int(seed_s), six[int(seed_s)]
        m = {"6way": metrics("6way", None, w)}
        for n in ALGOS:
            m[n] = metrics(n, data[n].get(seed), w)
        approx += m["6way"]["approx"]
        row = {"seed": seed, "scramble": scramble, "6way_htm_before_rephase": w["initial_htm"], "6way_saved": w["initial_htm"] - w["htm"]}
        for n, x in m.items():
            row.update({f"{n}_htm": x["htm"], f"{n}_time_to_final_ms": None if x["final_ms"] is None else round(x["final_ms"], 3),
                        f"{n}_wall_ms": None if x["wall_ms"] is None else round(x["wall_ms"], 3),
                        f"{n}_peak_mib": None if x["mib"] is None else round(x["mib"], 1)})
            if n == "optimal":
                o = data["optimal"].get(seed)
                row["optimal_status"] = "n/a" if o is None else ("proven" if x["ok"] else "unfinished")
                row["optimal_lower_bound"] = None if o is None or x["ok"] else o.get("lower_bound")
            if n != "6way":
                ok = x["ok"]
                row[f"diff_vs_{n}_htm"] = m["6way"]["htm"] - x["htm"] if ok else None  # negative: 6way shorter
                row[f"diff_vs_{n}_time_to_final_ms"] = round(m["6way"]["final_ms"] - x["final_ms"], 3) if x["final_ms"] is not None else None
                row[f"diff_vs_{n}_wall_ms"] = round(m["6way"]["wall_ms"] - x["wall_ms"], 3) if x["wall_ms"] is not None else None
                row[f"diff_vs_{n}_peak_mib"] = round(m["6way"]["mib"] - x["mib"], 1) if x["mib"] is not None else None
            long_rows.append({"seed": seed, "algorithm": n, "htm": x["htm"], "time_to_final_ms": row[f"{n}_time_to_final_ms"],
                              "wall_ms": row[f"{n}_wall_ms"], "peak_mib": row[f"{n}_peak_mib"], "moves": x["moves"]})
        rows.append(row)
    for path, rs in (("compare.csv", rows), ("compare_long.csv", long_rows)):
        with open(OUT / path, "w", newline="") as f:
            wr = csv.DictWriter(f, rs[0].keys())
            wr.writeheader()
            wr.writerows(rs)

    def avg(key, subset=rows):
        v = [r[key] for r in subset if r[key] is not None]
        return statistics.mean(v) if v else float("nan")
    proven = [r for r in rows if r["optimal_htm"] is not None]
    L = [f"cases (rephase success seeds): {len(rows)}",
         f"6way moves before rephase {avg('6way_htm_before_rephase'):.2f} -> after {avg('6way_htm'):.2f} (avg -{avg('6way_saved'):.2f})", "",
         f"{'algorithm':<14}{'avg_moves':>10}{'time_to_final_ms':>18}{'wall_ms':>10}{'peak_MiB':>10} | vs 6way (6way - other): {'d_moves':>8}{'d_final_ms':>12}{'d_wall_ms':>11}{'d_MiB':>9}"]
    for n in ("6way",) + ALGOS:
        sub = proven if n == "optimal" else rows
        line = f"{LABEL[n]:<14}{avg(f'{n}_htm', sub):10.2f}{avg(f'{n}_time_to_final_ms', sub):18.1f}{avg(f'{n}_wall_ms'):10.1f}{avg(f'{n}_peak_mib'):10.1f}"
        if n != "6way":
            line += f" |{'':24}{avg(f'diff_vs_{n}_htm', sub):+8.2f}{avg(f'diff_vs_{n}_time_to_final_ms', sub):+12.1f}{avg(f'diff_vs_{n}_wall_ms'):+11.1f}{avg(f'diff_vs_{n}_peak_mib'):+9.1f}"
        L.append(line)
    L += ["", "per seed (moves): 6way_before->after | stemil | kociemba | kociemba_sym | optimal (>=N: cap reached, proven lower bound)",
          f"{'seed':>6}  {'6way':>8}  {'stemil':>6}  {'kociemba':>8}  {'koc_sym':>7}  {'optimal':>8}"]
    for r in rows:
        opt = str(r["optimal_htm"]) if r["optimal_htm"] is not None else f">={r['optimal_lower_bound']}"
        L.append(f"{r['seed']:>6}  {str(r['6way_htm_before_rephase']) + '->' + str(r['6way_htm']):>8}  {r['stemil_htm']!s:>6}  "
                 f"{r['kociemba_htm']!s:>8}  {r['hk_symmetry_htm']!s:>7}  {opt:>8}")
    L += ["", f"optimal proven: {len(proven)}/{len(rows)} ({100 * len(proven) / len(rows):.1f}%); optimal's moves/time_to_final/diffs use proven cases only,"
              f" wall_ms and MiB use all (unfinished avg lower bound {avg('optimal_lower_bound'):.2f})"]
    for n in ALGOS:
        d = [r[f"diff_vs_{n}_htm"] for r in rows if r[f"diff_vs_{n}_htm"] is not None]
        L.append(f"6way vs {LABEL[n]}: moves shorter {sum(x < 0 for x in d)} / equal {sum(x == 0 for x in d)} / longer {sum(x > 0 for x in d)}")
    L += ["", "time_to_final_ms: when the final answer first existed (hindsight, same rule for all); wall_ms: real elapsed (6way/stemil use full budget).",
          "6way and optimal/kociemba_sym use 6 threads; others 1 thread. peak_MiB = process peak RSS incl. tables."]
    if approx:
        L.append(f"note: {approx} case(s) lack rephase_found_ms (older scan data); approximated as final_found_ms - two_phase_ms (error ~<1ms).")
    text = "\n".join(L)
    (OUT / "summary.txt").write_text(text + "\n")
    print(text)
    log(f"report 완료 -> {OUT / 'summary.txt'}, compare.csv, compare_long.csv")


def main():
    global OUT
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("stage", choices=("scan", "solve", "report", "all"))
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--count", type=int, default=20000, help="seeds scanned: seed..seed+count-1")
    p.add_argument("--chunk", type=int, default=200, help="seeds per 6way process (tables loaded once per chunk)")
    p.add_argument("--max-cases", type=int, default=10, help="stop scanning once this many rephase successes are found")
    p.add_argument("--scramble-length", type=int, default=30)
    p.add_argument("--two-phase-ms", type=float, default=500)
    p.add_argument("--rephase-ms", type=float, default=100)
    p.add_argument("--window", type=int, default=8)
    p.add_argument("--optimal-cap", type=float, default=60)
    p.add_argument("--out", type=Path, default=OUT, help="output directory (default compare/out)")
    a = p.parse_args()
    OUT = a.out.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    if a.stage in ("scan", "all"): scan(a)
    if a.stage in ("solve", "all"): solve(a)
    if a.stage in ("report", "all"): report(a)


if __name__ == "__main__":
    main()
