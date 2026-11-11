#!/usr/bin/env python3
"""Same-seed comparison of one rephase SUBJECT against stemil, kociemba, kociemba_sym (hkociemba) and optimal,
on the first N seeds where the subject's own rephase shortened its solution.

Subjects (--subject):
  6way            stemil_rephase_6way: 6 orientations, 1 candidate, phase1 ida, Two-Phase 500 ms + rephase <=100 ms
  stemil_rephase  ../build/stemil-rephase: original stemil Two-Phase (prefix) 500 ms + rephase <=100 ms
  rephase         kociemba first solution (<=24 moves) + rephase <=100 ms (../../rephase)
All: window 8, boundary+sliding, scramble length 30, seeds scanned from --seed upward.

  python3 compare/run_compare.py scan   --subject S --out DIR   # run S over seeds; keep cases where rephase shortened
  python3 compare/run_compare.py solve  --subject S --out DIR   # run the four other solvers on exactly those scrambles
  python3 compare/run_compare.py report --subject S --out DIR   # summary.md, README.md, summary.txt, compare*.csv
  python3 compare/run_compare.py all    --subject S --out DIR

Output layout: DIR/{summary.md, README.md, summary.txt, compare.csv, compare_long.csv, cases.tsv}
               DIR/data/<algorithm>.jsonl   DIR/scan/scan_<first seed>.jsonl (+ .stderr/.trace.jsonl)   DIR/logs/
Solvers run one after another, never concurrently. Tables are loaded before timing. Every stage is resumable.

Two time columns per algorithm:
  time_to_final_ms  when the FINAL answer first existed, in hindsight, same rule for everyone
                    (subject = best Two-Phase solution found + rephase improvement offset; stemil = last improvement;
                    kociemba/sym/optimal = the single call)
  wall_ms           real time the run took (6way/stemil/stemil_rephase use their whole Two-Phase budget)
"""
import argparse, csv, json, os, statistics, subprocess, sys, time
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent                      # 6way/
REPHASE_DIR = Path(os.environ.get("REPHASE_DIR", ROOT.parent / "rephase"))                 # <repo>/rephase (has .venv + hkociemba tables)
PY = REPHASE_DIR / ".venv/bin/python"
RES = Path(os.environ.get("STEMIL_RESOURCES", ROOT.parent / "third_party/stemil/resources"))  # stemil pruning tables (generated, not in git)
ALGOS = ("stemil", "kociemba", "hk_symmetry", "optimal")
LABEL = {"6way": "6way+rephase", "stemil_rephase": "stemil_rephase", "rephase": "rephase", "stemil": "stemil",
         "kociemba": "kociemba", "hk_symmetry": "kociemba_sym", "optimal": "optimal"}
THREADS = {"6way": 6, "stemil_rephase": 1, "rephase": 1, "stemil": 1, "kociemba": 1, "hk_symmetry": 6, "optimal": 6}
OUT = None


def log(msg):
    line = f"[{time.strftime('%H:%M:%S')}] {msg}"
    print(line, flush=True)
    if OUT:
        with open(OUT / "logs" / "run.log", "a") as f:
            f.write(line + "\n")


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


# ---------------------------------------------------------------- subject-specific scan
_scrambles = {}


def scrambles_for(a, start, n):
    """seed -> scramble for the rephase subject, identical to what the stemil shuffle gives for that seed."""
    if not _scrambles:
        for f in list((OUT / "scan").glob("scan_*.jsonl")) + list((OUT / "scan").glob("generated_scrambles_*.jsonl")):
            _scrambles.update({r["seed"]: r["scramble"] for r in jsonl(f)})
    missing = [s for s in range(start, start + n) if s not in _scrambles]
    if missing:  # generate a block with the stemil shuffle (search time is irrelevant, only `scramble` is used)
        g0, gn = missing[0], 5000
        f = OUT / "scan" / f"generated_scrambles_{g0}.jsonl"
        log(f"  스크램블 생성: seed {g0}~{g0 + gn - 1} (stemil shuffle, {a.scramble_length}수)")
        stream([ROOT / "build/stemil-rephase", "solve", "--json", "--seed", g0, "--runs", gn,
                "--scramble-length", a.scramble_length, "--budget-ms", 1, "--rephase-ms", 0, "--resources", RES,
                "--output-file", "/dev/null"], out=f, err=OUT / "logs" / "generated_scrambles.stderr")
        _scrambles.update({r["seed"]: r["scramble"] for r in jsonl(f)})
    return [(s, _scrambles[s]) for s in range(start, start + n)]


def add_trace_times(recs, trace_path):
    """stemil_rephase: hindsight time-to-final from the trace, same rule as 6way:
    initial_found_ms = last Two-Phase improvement - cube start; rephase_found_ms = last repair_applied - two_phase_end.
    Cube start = its shuffle event (the first cube's shuffle precedes table setup, so max(shuffle, setup_end))."""
    ev = [json.loads(l) for l in Path(trace_path).read_text().splitlines() if l.startswith("{")]
    setup_end = next(e["elapsed_ms"] for e in ev if e["event"] == "setup_end")
    per = {}
    for e in ev:
        i = e["index"]
        if i < 0:
            continue
        d = per.setdefault(i, {})
        if e["event"] == "shuffle":
            d["start"] = max(e["elapsed_ms"], setup_end)
        elif e["event"] in ("initial_solution", "candidate_improved"):
            d["found"] = e["elapsed_ms"]
        elif e["event"] == "two_phase_end":
            d["tp_end"] = e["elapsed_ms"]
        elif e["event"] == "repair_applied":
            d["rep"] = e["elapsed_ms"]
    for r in recs:
        d = per[r["index"]]
        r["initial_found_ms"] = round(d["found"] - d["start"], 3)
        r["rephase_found_ms"] = round(d["rep"] - d["tp_end"], 3) if "rep" in d else 0.0
    return recs


def scan_chunk(a, start, n, f):
    """Run the subject on seeds start..start+n-1 and write their records to f (atomically)."""
    tmp, err = f.with_suffix(".tmp"), f.with_suffix(".stderr")
    stdin = None
    if a.subject == "6way":
        cmd = [ROOT / "build/stemil-rephase", "solve", "--json", "--seed", start, "--runs", n,
               "--scramble-length", a.scramble_length, "--orientations", 6, "--candidates", 1, "--phase1-mode", "ida",
               "--budget-ms", a.two_phase_ms + a.rephase_ms, "--rephase-ms", a.rephase_ms, "--windows", a.window]
    elif a.subject == "stemil_rephase":
        trace = f.with_suffix(".trace.jsonl")
        trace.unlink(missing_ok=True)  # --trace-file never overwrites
        cmd = [ROOT / "build/stemil-rephase", "solve", "--json", "--seed", start, "--runs", n,
               "--scramble-length", a.scramble_length, "--budget-ms", a.two_phase_ms + a.rephase_ms,
               "--rephase-ms", a.rephase_ms, "--windows", a.window, "--resources", RES,
               "--output-file", OUT / "logs" / "shortened_cases.txt", "--trace-file", trace]
    else:
        stdin = OUT / "scan" / ".input.tsv"
        stdin.write_text("".join(f"{s}\t{sc}\n" for s, sc in scrambles_for(a, start, n)))
        cmd = [PY, "-I", HERE / "py_solvers.py", "rephase", a.window, a.rephase_ms]
    stream(cmd, stdin=stdin, out=tmp, err=err)
    recs = jsonl(tmp)
    if a.subject == "stemil_rephase":
        recs = add_trace_times(recs, f.with_suffix(".trace.jsonl"))
    tmp.write_text("".join(json.dumps(r) + "\n" for r in recs))
    tmp.replace(f)
    if stdin:
        stdin.unlink(missing_ok=True)
    return recs


# ---------------------------------------------------------------- scan
def scan(a):
    found = {}
    for f in sorted((OUT / "scan").glob("scan_*.jsonl")):
        if not f.name.endswith(".trace.jsonl"):
            found.update({r["seed"]: r for r in jsonl(f)})
    wins_upto = lambda end: sum(1 for s, r in found.items() if s < end and is_win(r))
    log(f"[{a.subject}] scan 시작: seed {a.seed}~, 목표 rephase 성공 {a.max_cases}건, chunk {a.chunk}개 "
        f"(기존 결과 재사용: {len(found)}개 큐브, 성공 {wins_upto(10**12)}건)")
    t0, done = time.time(), 0
    for start in range(a.seed, a.seed + a.count, a.chunk):
        n = min(a.chunk, a.seed + a.count - start)
        f = OUT / "scan" / f"scan_{start}.jsonl"
        if len(jsonl(f)) != n:  # missing or partial chunk: redo
            t = time.time()
            recs = scan_chunk(a, start, n, f)
            if len(recs) != n:
                sys.exit(f"[실패] seed {start}~{start + n - 1}: 결과 {len(recs)}/{n}건. {f.with_suffix('.stderr')} 확인")
            found.update({r["seed"]: r for r in recs})
            for r in recs:
                if is_win(r):
                    log(f"  성공 seed {r['seed']}: {r['initial_htm']}수 -> {r['htm']}수 (-{r['initial_htm'] - r['htm']})")
            done += n
            w, scanned = wins_upto(start + n), start + n - a.seed
            eta = ""
            if w:
                rate = w / scanned
                eta = (f" | 성공률 {100 * rate:.2f}%, 남은 예상 {(a.max_cases - w) / rate:.0f}개 큐브 "
                       f"≈ {(a.max_cases - w) / rate * (time.time() - t0) / done / 60:.0f}분")
            log(f"scan seed {start}~{start + n - 1} 완료 ({time.time() - t:.0f}s) | 누적 성공 {w}/{a.max_cases}{eta}")
        if wins_upto(start + n) >= a.max_cases:
            break
    wins = [r for s, r in sorted(found.items()) if is_win(r)][: a.max_cases]
    (OUT / "cases.tsv").write_text("".join(f"{r['seed']}\t{r['scramble']}\n" for r in wins))
    (OUT / "data" / f"{a.subject}.jsonl").write_text("".join(json.dumps(r) + "\n" for r in wins))
    log(f"[{a.subject}] scan 완료: 성공 case {len(wins)}건 -> {OUT / 'cases.tsv'}")
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
        f = OUT / "data" / f"{name}.jsonl"
        if len(jsonl(f)) == n:
            log(f"{name}: 기존 결과 {n}건 재사용")
            continue
        log(f"{name} 시작 (case {n}건). 테이블 로딩 때문에 첫 결과까지 오래 걸릴 수 있음"
            + (f", case당 최대 {a.optimal_cap:.0f}s" if name == "optimal" else ""))
        t, count = time.time(), [0]

        def on(r, name=name):
            count[0] += 1
            log(f"  {name} {count[0]}/{n} seed {r['seed']}: {describe(name, r)}")
        stream(cmd, stdin=cases, out=f, err=OUT / "logs" / f"{name}.stderr", on_line=on)
        log(f"{name} 완료 ({time.time() - t:.0f}s)")


# ---------------------------------------------------------------- report
def metrics(name, r, subject):
    """-> dict(htm, final_ms, wall_ms, mib, moves, ok, approx) for one algorithm on one case."""
    if name == subject:
        w = r
        if name == "6way":
            off = w.get("rephase_found_ms")
            approx = off is None  # scan chunk written before the field existed
            if approx:
                off = max(0.0, w["final_found_ms"] - w["two_phase_ms"])
            final, wall = w["initial_found_ms"] + off, w["elapsed_ms"]
        elif name == "stemil_rephase":
            approx, final, wall = False, w["initial_found_ms"] + w["rephase_found_ms"], w["elapsed_ms"]
        else:  # rephase
            approx, final, wall = False, w["found_ms"], w["ms"]
        return dict(htm=w["htm"], final_ms=final, wall_ms=wall, mib=w["peak_rss_kib"] / 1024,
                    moves=w.get("sequence") or w.get("moves"), ok=True, approx=approx)
    if r is None:
        return dict(htm=None, final_ms=None, wall_ms=None, mib=None, moves=None, ok=False, approx=False)
    ok = r["solved"] and r.get("htm") is not None
    if name == "stemil":
        final, wall = r["found_ms"], r["total_ms"]
    else:
        final, wall = (r["ms"] if ok else None), r["ms"]
    return dict(htm=r["htm"] if ok else None, final_ms=final, wall_ms=wall, mib=r["peak_rss_kib"] / 1024,
                moves=r.get("moves") if ok else None, ok=ok, approx=False)


def scan_stats(cases):
    """How many seeds were scanned up to the last selected case, and how many of them were rephase successes."""
    last = max(int(s) for s, _ in cases)
    seen, wins = 0, 0
    for f in (OUT / "scan").glob("scan_*.jsonl"):
        if f.name.endswith(".trace.jsonl"):
            continue
        for r in jsonl(f):
            if r["seed"] <= last:
                seen += 1
                wins += is_win(r)
    return seen, wins, last


def report(a):
    S = a.subject
    if not (OUT / "cases.tsv").exists():
        sys.exit("cases.tsv가 없습니다. 먼저 scan을 실행하세요.")
    cases = [l.split("\t") for l in (OUT / "cases.tsv").read_text().splitlines()]
    if not cases:
        sys.exit("rephase 성공 case가 0건입니다. scan 범위/조건을 확인하세요.")
    subj = {r["seed"]: r for r in jsonl(OUT / "data" / f"{S}.jsonl")}
    data = {n: {r["seed"]: r for r in jsonl(OUT / "data" / f"{n}.jsonl")} for n in ALGOS}
    rows, long_rows, approx = [], [], 0
    for seed_s, scramble in cases:
        seed, w = int(seed_s), subj[int(seed_s)]
        m = {S: metrics(S, w, S)}
        for n in ALGOS:
            m[n] = metrics(n, data[n].get(seed), S)
        approx += m[S]["approx"]
        row = {"seed": seed, "scramble": scramble, f"{S}_htm_before_rephase": w["initial_htm"], f"{S}_saved": w["initial_htm"] - w["htm"]}
        for n, x in m.items():
            row.update({f"{n}_htm": x["htm"], f"{n}_time_to_final_ms": None if x["final_ms"] is None else round(x["final_ms"], 3),
                        f"{n}_wall_ms": None if x["wall_ms"] is None else round(x["wall_ms"], 3),
                        f"{n}_peak_mib": None if x["mib"] is None else round(x["mib"], 1)})
            if n == "optimal":
                o = data["optimal"].get(seed)
                row["optimal_status"] = "n/a" if o is None else ("proven" if x["ok"] else "unfinished")
                row["optimal_lower_bound"] = None if o is None or x["ok"] else o.get("lower_bound")
            if n != S:
                ok = x["ok"]
                row[f"diff_vs_{n}_htm"] = m[S]["htm"] - x["htm"] if ok else None  # negative: subject shorter
                row[f"diff_vs_{n}_time_to_final_ms"] = round(m[S]["final_ms"] - x["final_ms"], 3) if x["final_ms"] is not None else None
                row[f"diff_vs_{n}_wall_ms"] = round(m[S]["wall_ms"] - x["wall_ms"], 3) if x["wall_ms"] is not None else None
                row[f"diff_vs_{n}_peak_mib"] = round(m[S]["mib"] - x["mib"], 1) if x["mib"] is not None else None
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
    sub = lambda n: proven if n == "optimal" else rows
    wld = {}
    for n in ALGOS:
        d = [r[f"diff_vs_{n}_htm"] for r in rows if r[f"diff_vs_{n}_htm"] is not None]
        wld[n] = (sum(x < 0 for x in d), sum(x == 0 for x in d), sum(x > 0 for x in d))

    # ---- summary.txt (plain; the 6way layout is unchanged from the original report)
    L = [f"cases (rephase success seeds): {len(rows)}",
         f"{S} moves before rephase {avg(f'{S}_htm_before_rephase'):.2f} -> after {avg(f'{S}_htm'):.2f} (avg -{avg(f'{S}_saved'):.2f})", "",
         f"{'algorithm':<14}{'avg_moves':>10}{'time_to_final_ms':>18}{'wall_ms':>10}{'peak_MiB':>10} | vs {S} ({S} - other): {'d_moves':>8}{'d_final_ms':>12}{'d_wall_ms':>11}{'d_MiB':>9}"]
    for n in (S,) + ALGOS:
        line = f"{LABEL[n]:<14}{avg(f'{n}_htm', sub(n)):10.2f}{avg(f'{n}_time_to_final_ms', sub(n)):18.1f}{avg(f'{n}_wall_ms'):10.1f}{avg(f'{n}_peak_mib'):10.1f}"
        if n != S:
            line += f" |{'':24}{avg(f'diff_vs_{n}_htm', sub(n)):+8.2f}{avg(f'diff_vs_{n}_time_to_final_ms', sub(n)):+12.1f}{avg(f'diff_vs_{n}_wall_ms'):+11.1f}{avg(f'diff_vs_{n}_peak_mib'):+9.1f}"
        L.append(line)
    L += ["", f"per seed (moves): {S}_before->after | stemil | kociemba | kociemba_sym | optimal (>=N: cap reached, proven lower bound)",
          f"{'seed':>6}  {S:>8}  {'stemil':>6}  {'kociemba':>8}  {'koc_sym':>7}  {'optimal':>8}"]
    for r in rows:
        opt = str(r["optimal_htm"]) if r["optimal_htm"] is not None else f">={r['optimal_lower_bound']}"
        L.append(f"{r['seed']:>6}  {str(r[f'{S}_htm_before_rephase']) + '->' + str(r[f'{S}_htm']):>8}  {r['stemil_htm']!s:>6}  "
                 f"{r['kociemba_htm']!s:>8}  {r['hk_symmetry_htm']!s:>7}  {opt:>8}")
    L += ["", f"optimal proven: {len(proven)}/{len(rows)} ({100 * len(proven) / len(rows):.1f}%); optimal's moves/time_to_final/diffs use proven cases only,"
              f" wall_ms and MiB use all (unfinished avg lower bound {avg('optimal_lower_bound'):.2f})"]
    for n in ALGOS:
        L.append(f"{S} vs {LABEL[n]}: moves shorter {wld[n][0]} / equal {wld[n][1]} / longer {wld[n][2]}")
    L += ["", "time_to_final_ms: when the final answer first existed (hindsight, same rule for all); wall_ms: real elapsed (6way/stemil use full budget).",
          "6way and optimal/kociemba_sym use 6 threads; others 1 thread. peak_MiB = process peak RSS incl. tables."]
    if approx:
        L.append(f"note: {approx} case(s) lack rephase_found_ms (older scan data); approximated as final_found_ms - two_phase_ms (error ~<1ms).")
    (OUT / "summary.txt").write_text("\n".join(L) + "\n")

    # ---- summary.md (readable)
    seen, wins, last = scan_stats(cases)
    subject_setting = {
        "6way": f"stemil_rephase_6way: 6방향 동시 탐색, 후보 1개, Phase 1 ida, Two-Phase {a.two_phase_ms:g} ms + rephase 최대 {a.rephase_ms:g} ms",
        "stemil_rephase": f"stemil_rephase: 원본 stemil Two-Phase(prefix) {a.two_phase_ms:g} ms + rephase 최대 {a.rephase_ms:g} ms",
        "rephase": f"rephase: kociemba Two-Phase 첫 해(최대 24수) + rephase 최대 {a.rephase_ms:g} ms",
    }[S]
    f1 = lambda v: "-" if v != v else f"{v:.2f}"
    f0 = lambda v: "-" if v != v else f"{v:,.1f}"
    sg = lambda v: "-" if v != v else f"{v:+.2f}"
    M = [f"# {LABEL[S]} 비교 결과", "",
         f"{LABEL[S]}에서 rephase 단계가 실제로 해를 줄인 큐브 {len(rows)}개를 골라, 같은 스크램블에서 다른 알고리즘 4개와 비교한 결과다.", "",
         "## 실험 조건", "",
         "| 항목 | 값 |", "|---|---|",
         f"| 기준 알고리즘 | {subject_setting} |",
         f"| rephase 설정 | window {a.window}, boundary+sliding, HTM |",
         f"| 스크램블 | {a.scramble_length}수, seed {a.seed}부터 순서대로 (stemil shuffle) |",
         f"| case 선정 | seed {a.seed}~{last}의 {seen:,}개 중 rephase 성공 {wins}건 ({100 * wins / seen:.2f}%) 중 앞 {len(rows)}건 |",
         f"| stemil | 원본 stemil, {a.two_phase_ms:g} ms |",
         "| kociemba | pip kociemba(C two-phase), 첫 해(최대 24수) |",
         "| kociemba_sym | hkociemba RubikTwoPhase(대칭 축소), 첫 해, 6스레드 |",
         f"| optimal | IDA* 최적해, case당 최대 {a.optimal_cap:g}초, 6스레드 |", "",
         "## 알고리즘별 평균", "",
         "| 알고리즘 | 평균 move | 최종 해까지 (ms) | 실행 시간 (ms) | 메모리 (MiB) | 스레드 |", "|---|---:|---:|---:|---:|---:|"]
    for n in (S,) + ALGOS:
        name = f"**{LABEL[n]}**" if n == S else LABEL[n] + (" ※" if n == "optimal" else "")
        M.append(f"| {name} | {f1(avg(f'{n}_htm', sub(n)))} | {f0(avg(f'{n}_time_to_final_ms', sub(n)))} | "
                 f"{f0(avg(f'{n}_wall_ms'))} | {f0(avg(f'{n}_peak_mib'))} | {THREADS[n]} |")
    M += ["", f"※ optimal의 move·최종 해 시간은 최적해 증명이 끝난 {len(proven)}건 평균이다. 실행 시간·메모리는 {len(rows)}건 전체 평균이고, "
          f"미확정 {len(rows) - len(proven)}건의 평균 하한은 {f1(avg('optimal_lower_bound'))}수다.", "",
          f"## {LABEL[S]}와의 차이", "",
          f"차이는 `{LABEL[S]} - 다른 알고리즘`이다. move 차이가 음수면 {LABEL[S]}가 더 짧다.", "",
          f"| 비교 대상 | move 차이 | {LABEL[S]}가 더 짧음 | 같음 | 더 김 | 최종 해 시간 차이 (ms) | 실행 시간 차이 (ms) | 메모리 차이 (MiB) |",
          "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for n in ALGOS:
        M.append(f"| {LABEL[n]}{' ※' if n == 'optimal' else ''} | {sg(avg(f'diff_vs_{n}_htm', sub(n)))} | {wld[n][0]} | {wld[n][1]} | {wld[n][2]} | "
                 f"{sg(avg(f'diff_vs_{n}_time_to_final_ms', sub(n)))} | {sg(avg(f'diff_vs_{n}_wall_ms'))} | {sg(avg(f'diff_vs_{n}_peak_mib'))} |")
    saved = Counter(r[f"{S}_saved"] for r in rows)
    M += ["", f"## {LABEL[S]}의 rephase 효과", "",
          f"rephase 전 평균 {f1(avg(f'{S}_htm_before_rephase'))}수에서 rephase 후 {f1(avg(f'{S}_htm'))}수로, 평균 {f1(avg(f'{S}_saved'))}수 줄었다.", "",
          "| 줄어든 수 | case 수 |", "|---:|---:|"] + [f"| {k} | {saved[k]} |" for k in sorted(saved)]
    if proven:
        gap = Counter(r[f"{S}_htm"] - r["optimal_htm"] for r in proven)
        M += ["", "## 최적해와의 차이", "",
              f"최적해 증명이 끝난 {len(proven)}건에서 `{LABEL[S]} - 최적` 분포다.", "",
              "| 최적보다 긴 수 | case 수 |", "|---:|---:|"] + [f"| {k} | {gap[k]} |" for k in sorted(gap)]
    M += ["", "## 측정 규칙", "",
          "- **최종 해까지 시간:** 최종 해가 처음 존재한 시각을 사후에 잰 값이다. 모든 알고리즘에 같은 규칙을 쓴다. "
          f"{LABEL[S]}는 가장 좋은 Two-Phase 해를 찾은 시각에 rephase가 개선한 시각의 오프셋을 더한다. stemil은 마지막 개선 시각이고, "
          "kociemba·kociemba_sym·optimal은 한 번의 호출 시간이다.",
          "- **실행 시간:** 실제로 걸린 시간이다. 6way·stemil·stemil_rephase는 Two-Phase 시간 예산을 끝까지 쓴다.",
          "- **메모리:** 테이블을 포함한 프로세스 peak RSS다.",
          "- **실행 방식:** 알고리즘은 하나씩 순서대로 실행하고, 시간은 테이블 로딩 이후부터 잰다.",
          f"- **주의:** case는 {LABEL[S]}의 rephase가 성공한 큐브만 고른 것이다. {LABEL[S]}에 유리한 표본이며, 무작위 큐브 전체의 평균과는 다르다."]
    if approx:
        M.append(f"- {approx}건은 오래된 scan 기록이라 rephase 오프셋을 `final_found_ms - two_phase_ms`로 근사했다(오차 1ms 미만).")
    M += ["", "## 시드별 결과 (move 수)", "",
          f"`>=N`은 optimal이 시간 상한에 걸려 증명한 하한이 N수라는 뜻이다.", "",
          f"| seed | {LABEL[S]} (전→후) | stemil | kociemba | kociemba_sym | optimal |", "|---:|---:|---:|---:|---:|---:|"]
    for r in rows:
        opt = str(r["optimal_htm"]) if r["optimal_htm"] is not None else f">={r['optimal_lower_bound']}"
        M.append(f"| {r['seed']} | {r[f'{S}_htm_before_rephase']} → {r[f'{S}_htm']} | {r['stemil_htm']} | {r['kociemba_htm']} | "
                 f"{r['hk_symmetry_htm']} | {opt} |")
    (OUT / "summary.md").write_text("\n".join(M) + "\n")

    # ---- README.md
    out_rel = os.path.relpath(OUT, ROOT)
    R = [f"# {LABEL[S]} compare", "",
         f"{LABEL[S]}를 stemil, kociemba, kociemba_sym, optimal과 같은 스크램블에서 비교한 결과다. 결과 표는 `summary.md`에 있다.", "",
         "## 파일", "",
         "| 경로 | 내용 |", "|---|---|",
         "| `summary.md` | 결과 요약 표(가장 먼저 볼 파일) |",
         "| `summary.txt` | 같은 요약의 일반 텍스트판 |",
         "| `compare.csv` | case 하나가 한 행인 전체 수치와 차이 |",
         "| `compare_long.csv` | 알고리즘별 한 행으로 펼친 수치와 해 수순 |",
         f"| `cases.tsv` | 비교에 쓴 {len(rows)}개 seed와 스크램블 |",
         f"| `data/{S}.jsonl` | {LABEL[S]} 원자료({len(rows)}건) |",
         "| `data/<알고리즘>.jsonl` | stemil·kociemba·hk_symmetry(kociemba_sym)·optimal 원자료 |",
         f"| `scan/` | case 선정용 scan 기록. seed {a.chunk}개 단위 `scan_<첫 seed>.jsonl`과 그 로그 |",
         "| `logs/` | 실행 로그(`run.log`)와 각 알고리즘의 stderr |", "",
         "## 재실행", "",
         "`stemil-rephase/` 디렉터리에서 실행한다. 각 단계는 이어서 실행할 수 있다.", "",
         "```bash",
         f'python3 compare/run_compare.py all --subject {S} --out "{out_rel}" --max-cases {len(rows)}',
         "```"]
    (OUT / "README.md").write_text("\n".join(R) + "\n")
    print("\n".join(L))
    log(f"report 완료 -> {OUT / 'summary.md'}, README.md, summary.txt, compare.csv, compare_long.csv")


def main():
    global OUT
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("stage", choices=("scan", "solve", "report", "all"))
    p.add_argument("--subject", choices=("6way", "stemil_rephase", "rephase"), required=True)
    p.add_argument("--out", type=Path, required=True, help='output directory, e.g. "rephase compare"')
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--count", type=int, default=1_000_000, help="seeds scanned at most: seed..seed+count-1")
    p.add_argument("--chunk", type=int, default=200, help="seeds per subject process (tables loaded once per chunk)")
    p.add_argument("--max-cases", type=int, default=200, help="stop scanning once this many rephase successes are found")
    p.add_argument("--scramble-length", type=int, default=30)
    p.add_argument("--two-phase-ms", type=float, default=500)
    p.add_argument("--rephase-ms", type=float, default=100)
    p.add_argument("--window", type=int, default=8)
    p.add_argument("--optimal-cap", type=float, default=60)
    a = p.parse_args()
    OUT = a.out.resolve()
    for d in ("scan", "data", "logs"):
        (OUT / d).mkdir(parents=True, exist_ok=True)
    if a.stage in ("scan", "all"): scan(a)
    if a.stage in ("solve", "all"): solve(a)
    if a.stage in ("report", "all"): report(a)


if __name__ == "__main__":
    main()
