#!/usr/bin/env python3
"""Repeat random solves; print only verified improvements of at least one HTM.

Each trial runs in a fresh process. New-runner-only native instrumentation
measures actual Phase-1/2 search time and sampled process RSS by active phase.
"""
import argparse
import json
import math
import multiprocessing as mp
from pathlib import Path
import random
import secrets
import signal
import sys
import time

from boundary_repair import RepairConfig, format_moves, prepare_engine, repair_solution
from boundary_repair.cube import transform
from boundary_repair.scramble import random_scramble
from repeat_metrics.profiler import ProfiledSolver, build_profiler
from solve import attempt_record


def measure_trial(index, seed, binary, config, scramble_length=20,
                  base_seconds=3, max_length=24, sample_ms=0.5):
    preparation_start = time.perf_counter()
    profiler = ProfiledSolver(Path(binary))
    prepare_engine()
    preparation_ms = (time.perf_counter() - preparation_start) * 1000
    scramble = random_scramble(scramble_length, rng=random.Random(seed))
    initial = transform(scramble)
    profiler.start_monitor(sample_ms)
    try:
        base = profiler.solve(initial, base_seconds, max_length)
        profiler.begin_repair()
        result = repair_solution(base.moves, base.boundary, config, initial)
        profiler.end_repair()
    finally:
        metrics = profiler.stop_monitor()
    native_ms = metrics["total_ns"] / 1_000_000
    repair_exact_ms = sum(a.elapsed_ms for a in result.attempts)
    memory = {
        "method": "sampled process RSS during each active stage",
        "isolated_process_per_trial": False,  # controller sets this after a fresh worker responds
        "includes_shared_tables_and_runtime": True,
        "sample_interval_ms": sample_ms,
        "baseline_rss_bytes": metrics["baseline_rss"],
        "phase1_peak_rss_bytes": metrics["phase1_rss"] or None,
        "phase2_peak_rss_bytes": metrics["phase2_rss"] or None,
        "repair_peak_rss_bytes": metrics["repair_rss"] or None,
        "total_peak_rss_bytes": metrics["total_rss"] or None,
        "phase1_samples": metrics["phase1_samples"],
        "phase2_samples": metrics["phase2_samples"],
        "repair_samples": metrics["repair_samples"],
    }
    return {
        "trial": index, "seed": seed, "metric": "HTM",
        "scramble": format_moves(scramble), "scramble_length": len(scramble),
        "initial_solution": format_moves(base.moves), "boundary": base.boundary,
        "phase1_solution": format_moves(base.moves[:base.boundary]),
        "phase2_solution": format_moves(base.moves[base.boundary:]),
        "solution": format_moves(result.moves),
        "phase1_moves": base.boundary, "phase2_moves": len(base.moves) - base.boundary,
        "initial_total_moves": len(base.moves), "final_total_moves": len(result.moves),
        "saved_moves": result.saved_moves, "verified": result.verified,
        "global_optimality_proven": False,
        "phase1_ms": metrics["phase1_ns"] / 1_000_000,
        "phase2_ms": metrics["phase2_ns"] / 1_000_000,
        "twophase_ms": native_ms, "phase2_search_calls": metrics["phase2_calls"],
        "repair_exact_ms": repair_exact_ms, "repair_stage_ms": result.elapsed_ms,
        "pure_solve_ms": native_ms + repair_exact_ms,
        "pure_solve_definition": "native Two-Phase time + summed exact-search calls; profiling overhead included",
        "preparation_ms": preparation_ms, "memory": memory,
        "windows": sorted(set(config.windows)), "time_limit_ms": config.time_limit_ms,
        "per_window_ms": config.per_window_ms, "shifts": list(config.shifts),
        "sliding": config.sliding, "max_states": config.max_states,
        "base_seconds": base_seconds, "max_length": max_length,
        "stop_reason": result.stop_reason,
        "attempts": [attempt_record(a) for a in result.attempts],
    }


def _worker(connection, options):
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    try:
        connection.send({"record": measure_trial(**options)})
    except Exception as exc:
        connection.send({"error": f"{type(exc).__name__}: {exc}"})
    finally:
        connection.close()


def isolated_trial(options, timeout):
    context = mp.get_context("spawn")
    incoming, outgoing = context.Pipe(duplex=False)
    process = context.Process(target=_worker, args=(outgoing, options))
    start = time.perf_counter()
    received = False
    try:
        process.start()
        outgoing.close()
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError(f"시행 wall-clock 안전 한도 {timeout:g}초 초과")
            if incoming.poll(min(0.1, remaining)):
                break
            if not process.is_alive():
                raise RuntimeError(f"측정 프로세스가 응답 없이 종료했습니다: exit={process.exitcode}")
        try:
            message = incoming.recv()
        except EOFError as exc:
            raise RuntimeError("측정 프로세스 응답이 끊겼습니다") from exc
        received = True
        if "error" in message:
            raise RuntimeError(message["error"])
        record = message["record"]
        record["memory"]["isolated_process_per_trial"] = True
        record["trial_wall_ms"] = (time.perf_counter() - start) * 1000
        return record
    finally:
        incoming.close()
        outgoing.close()
        if process.pid is not None:
            if not received and process.is_alive():
                process.terminate()
            process.join(timeout=1)
            if process.is_alive():
                process.terminate()
                process.join(timeout=2)
            if process.is_alive():
                process.kill()
                process.join(timeout=2)
            process.close()


def _mib(value):
    return f"{value / 1_048_576:.3f}" if value is not None else "미관측"


def emit_hit(record, *, as_json=False, output=None, stream=None):
    """Filter at the output boundary, so non-hits never leak into stdout/JSONL."""
    if not record.get("verified") or record["saved_moves"] < 1:
        return False
    stream = stream if stream is not None else sys.stdout
    serialized = json.dumps(record, ensure_ascii=False)
    if output is not None:
        output.write(serialized + "\n")
        output.flush()
    if as_json:
        print(serialized, file=stream, flush=True)
    else:
        memory = record["memory"]
        print(f"\n시행 #{record['trial']} / seed={record['seed']} / {record['saved_moves']}수 단축", file=stream)
        print(f"스크램블: {record['scramble']}", file=stream)
        print(f"Two-Phase ({record['initial_total_moves']} HTM): "
              f"{record['phase1_solution']} | {record['phase2_solution']}", file=stream)
        print(f"최종 해 ({record['final_total_moves']} HTM): {record['solution']}", file=stream)
        print("단계               탐색 시간(ms)    수순 수(HTM)    관측최대 RSS(MiB)", file=stream)
        rows = (
            ("Phase 1", record["phase1_ms"], str(record["phase1_moves"]), memory["phase1_peak_rss_bytes"]),
            ("Phase 2", record["phase2_ms"], str(record["phase2_moves"]), memory["phase2_peak_rss_bytes"]),
            ("Two-Phase 합계", record["twophase_ms"], str(record["initial_total_moves"]),
             max(memory["phase1_peak_rss_bytes"] or 0, memory["phase2_peak_rss_bytes"] or 0) or None),
            ("Exact repair", record["repair_exact_ms"], f"-{record['saved_moves']}", memory["repair_peak_rss_bytes"]),
            ("전체 solve", record["pure_solve_ms"], str(record["final_total_moves"]), memory["total_peak_rss_bytes"]),
        )
        for name, elapsed, moves, rss in rows:
            print(f"{name:<18} {elapsed:>12.3f}    {moves:>10}    {_mib(rss):>18}", file=stream)
        print(f"Phase 2 진입 횟수: {record['phase2_search_calls']} / "
              f"큐브 검증: 통과 / 종료: {record['stop_reason']}", file=stream)
        for attempt in record["attempts"]:
            if attempt["status"] == "FOUND":
                print(f"window {attempt['window_size']} "
                      f"(당시 {attempt['start'] + 1}~{attempt['end']}수): "
                      f"{attempt['before']} -> {attempt['replacement'] or '(빈 수순)'}", file=stream)
        print(f"준비 {record['preparation_ms']:.3f} ms / "
              f"repair 단계 전체 {record['repair_stage_ms']:.3f} ms / "
              f"시행 wall time {record['trial_wall_ms']:.3f} ms (순수 solve와 별도)", file=stream, flush=True)
    return True


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=1000,
                        help="최대 반복 시행 수 (기본: 1000); 0이면 Ctrl+C까지 반복")
    parser.add_argument("--stop-after-hits", type=int, default=0,
                        help="단축된 해 N개 출력 후 종료; 0이면 개수 제한 없음")
    parser.add_argument("--seed", type=int, help="첫 시행의 scramble seed; 다음 시행은 seed+1")
    parser.add_argument("--scramble-length", type=int, default=20)
    parser.add_argument("--window", "--windows", type=int, nargs="+", default=[6, 8])
    parser.add_argument("--time-ms", type=float, default=100)
    parser.add_argument("--per-window-ms", type=float)
    parser.add_argument("--shifts", type=int, nargs="+", default=[0])
    parser.add_argument("--sliding", action="store_true")
    parser.add_argument("--max-states", type=int, default=200_000)
    parser.add_argument("--base-seconds", type=float, default=3)
    parser.add_argument("--max-length", type=int, default=24)
    parser.add_argument("--sample-ms", type=float, default=0.5,
                        help="RSS 관측 간격 (기본: 0.5 ms); 관측최대는 실제 peak보다 낮을 수 있음")
    parser.add_argument("--trial-timeout", type=float,
                        help="별도 프로세스의 wall-clock 안전 한도, 초; 자동 설정 가능")
    parser.add_argument("--progress-every", type=int, default=100,
                        help="N번마다 stderr에 진행 상황 출력; 0이면 진행 출력 생략")
    parser.add_argument("--json", action="store_true", help="단축된 결과만 JSONL로 stdout 출력")
    parser.add_argument("--output", type=Path, help="단축된 결과만 JSONL 파일에 추가 저장")
    return parser


def run(args):
    if args.count < 0 or args.stop_after_hits < 0 or args.progress_every < 0:
        raise ValueError("count, stop-after-hits, progress-every는 0 이상이어야 합니다")
    if not 1 <= args.scramble_length <= 100:
        raise ValueError("scramble-length는 1~100이어야 합니다")
    if not math.isfinite(args.base_seconds) or not 0 < args.base_seconds <= 3600:
        raise ValueError("base-seconds는 0 초과~3600의 유한한 값이어야 합니다")
    if not 1 <= args.max_length <= 30:
        raise ValueError("max-length는 1~30이어야 합니다")
    if not math.isfinite(args.sample_ms) or not 0.1 <= args.sample_ms <= 100:
        raise ValueError("sample-ms는 0.1~100의 유한한 값이어야 합니다")
    if args.trial_timeout is not None and (not math.isfinite(args.trial_timeout)
                                         or not 0 < args.trial_timeout <= 10_000):
        raise ValueError("trial-timeout은 0 초과~10000의 유한한 값이어야 합니다")
    config = RepairConfig(tuple(args.window), args.time_ms, args.per_window_ms,
                          tuple(args.shifts), args.sliding, args.max_states)
    timeout = (args.trial_timeout if args.trial_timeout is not None else
               max(30, math.ceil(args.base_seconds) + args.time_ms / 1000 + 15))
    master_seed = args.seed if args.seed is not None else secrets.randbits(64)
    print(f"반복 실행: count={args.count or 'Ctrl+C까지'}, "
          f"window={sorted(set(args.window))}, repair 예산={args.time_ms:g} ms, "
          f"첫 seed={master_seed}", file=sys.stderr, flush=True)
    print("1수 이상 단축된 해만 stdout에 출력합니다. Phase 수순 수는 초기 Two-Phase 기준입니다.\n"
          "메모리는 공유 테이블·런타임을 포함한 활성 Phase의 관측최대 process RSS이며 합산하지 않습니다.\n"
          "순수 solve = native Two-Phase + exact search 호출 시간; 계측 오버헤드는 포함됩니다.",
          file=sys.stderr, flush=True)
    binary = build_profiler()
    prepare_engine()
    attempted = completed = hits = errors = consecutive_errors = 0
    hit_summaries = []
    start = time.monotonic()
    output = None
    exit_code = 0
    try:
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            output = args.output.open("a", encoding="utf-8")
        while args.count == 0 or attempted < args.count:
            attempted += 1
            options = {"index": attempted, "seed": master_seed + attempted - 1,
                       "binary": str(binary), "config": config,
                       "scramble_length": args.scramble_length, "base_seconds": args.base_seconds,
                       "max_length": args.max_length, "sample_ms": args.sample_ms}
            try:
                record = isolated_trial(options, timeout)
            except RuntimeError as exc:
                errors += 1
                consecutive_errors += 1
                print(f"시행 #{attempted} 실패: {exc}", file=sys.stderr, flush=True)
                if consecutive_errors >= 3:
                    print("연속 3회 실패하여 반복을 중단합니다.", file=sys.stderr, flush=True)
                    exit_code = 2
                    break
                continue
            completed += 1
            consecutive_errors = 0
            if emit_hit(record, as_json=args.json, output=output):
                hits += 1
                replacements = sum(a["status"] == "FOUND" for a in record.get("attempts", ()))
                hit_summaries.append((attempted, record["saved_moves"], replacements))
                if args.stop_after_hits and hits >= args.stop_after_hits:
                    break
            if args.progress_every and attempted % args.progress_every == 0:
                print(f"진행: {attempted}회 시도 / {completed}회 검증 / "
                      f"단축 {hits}개 / 실패 {errors}개 / 경과 {time.monotonic() - start:.1f}초",
                      file=sys.stderr, flush=True)
    except KeyboardInterrupt:
        exit_code = 130
    finally:
        if output:
            output.close()
        print(f"종료: {attempted}회 시도 / {completed}회 검증 / "
              f"1수 이상 단축 {hits}개 / 실패 {errors}개", file=sys.stderr, flush=True)
        if hit_summaries:
            print("단축 시행별 결과:", file=sys.stderr)
            for trial, saved_moves, replacements in hit_summaries:
                print(f"  시행 #{trial}: {saved_moves}수 단축 (window 교체 {replacements}회)",
                      file=sys.stderr)
            print(f"총 단축: {sum(saved for _, saved, _ in hit_summaries)}수 / "
                  f"window 교체: {sum(count for _, _, count in hit_summaries)}회",
                  file=sys.stderr, flush=True)
        else:
            print("단축이 발생한 시행: 없음", file=sys.stderr, flush=True)
    if errors and not completed and exit_code == 0:
        exit_code = 2
    return exit_code


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        return run(args)
    except (ValueError, RuntimeError, OSError) as exc:
        print(f"오류: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
