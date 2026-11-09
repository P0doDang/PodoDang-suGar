#!/usr/bin/env python3
"""Command line entry point for Kociemba phase-boundary exact repair."""
import argparse
import json
from pathlib import Path
import random
import secrets
import sys
import time

from boundary_repair import (RepairConfig, format_moves, from_facelets,
                             initial_solution, parse_moves, repair_solution)
from boundary_repair.backends import DEFAULT_TABLE_DIR, prepare_reference
from boundary_repair.cube import transform
from boundary_repair.scramble import random_scramble


def _repair_options(parser):
    parser.add_argument("--window", "--windows", type=int, nargs="+", default=[6, 8],
                        help="선택한 window 길이; 작은 순서로 탐색 (기본: 6 8)")
    parser.add_argument("--time-ms", type=float, default=100,
                        help="전체 exact repair 단계의 공유 시간 예산, ms (기본: 100)")
    parser.add_argument("--per-window-ms", type=float,
                        help="선택 사항: 각 window의 추가 시간 상한, ms")
    parser.add_argument("--shifts", type=int, nargs="+", default=[0],
                        help="경계 중심 window의 시작 위치 이동량 (예: -1 0 1)")
    parser.add_argument("--sliding", action="store_true",
                        help="경계 탐색 후 남은 예산으로 전체 수순 sliding window 탐색")
    parser.add_argument("--max-states", type=int, default=200_000,
                        help="한 exact search가 보관할 양쪽 합계 state 수 상한")
    parser.add_argument("--trace", action="store_true", help="탐색 결과를 즉시 stderr에 출력")
    parser.add_argument("--json", action="store_true", help="결과를 JSON으로 출력")
    parser.add_argument("--output", type=Path, help="동일한 JSON 결과를 파일로 저장")


def build_parser():
    parser = argparse.ArgumentParser(description="Kociemba + Phase-Boundary Exact Repair (HTM)")
    sub = parser.add_subparsers(dest="command", required=True)
    solve = sub.add_parser("solve", help="자동 scramble 또는 입력 큐브를 Two-Phase로 푼 뒤 repair")
    source = solve.add_mutually_exclusive_group()
    source.add_argument("--random", action="store_true", help="자동 scramble 생성 (입력 생략 시 기본 동작)")
    source.add_argument("--scramble", help="solved cube에서 적용할 scramble 수순")
    source.add_argument("--facelets", help="URFDLB 면 순서의 54개 색 문자열")
    solve.add_argument("--scramble-length", type=int,
                       help="자동 생성할 scramble의 HTM 길이, 1~100 (기본: 20)")
    solve.add_argument("--seed", type=int,
                       help="자동 scramble 재현용 seed; 생략하면 매번 새 seed 생성")
    solve.add_argument("--backend", choices=["kociemba", "twophase"], default="kociemba",
                       help="기본은 빠른 native 구현; twophase는 hkociemba 참고 패키지")
    solve.add_argument("--base-seconds", type=float, default=3,
                       help="초기 Two-Phase 탐색 timeout; repair 예산과 별도")
    solve.add_argument("--max-length", type=int, default=24,
                       help="kociemba의 길이 상한 / twophase의 조기 종료 목표")
    solve.add_argument("--table-dir", type=Path, default=DEFAULT_TABLE_DIR,
                       help="참고 backend 전용 테이블 디렉터리")
    _repair_options(solve)
    repair = sub.add_parser("repair", help="이미 구한 해의 Phase 경계 주변만 repair")
    repair.add_argument("--solution", required=True,
                        help="기존 해; Phase 경계는 | 또는 . 로 표시 가능")
    repair.add_argument("--boundary", type=int,
                        help="Phase 1 move 수 (zero-based cut); 예: 9이면 9수 적용 후 경계")
    source = repair.add_mutually_exclusive_group()
    source.add_argument("--scramble", help="선택 사항: 기존 해가 풀어야 하는 scramble")
    source.add_argument("--facelets", help="선택 사항: 기존 해가 풀어야 하는 큐브")
    _repair_options(repair)
    init = sub.add_parser("init-twophase", help="참고 구현의 테이블을 명시적으로 최초 생성")
    init.add_argument("--table-dir", type=Path, default=DEFAULT_TABLE_DIR)
    return parser


def _source_state(args):
    if args.scramble is not None:
        return transform(parse_moves(args.scramble))
    if args.facelets is not None:
        return from_facelets(args.facelets)
    return None


def _existing_solution(text, explicit_boundary):
    separators = text.count("|") + text.count(".")
    if separators:
        if separators != 1:
            raise ValueError("Phase separator는 하나만 사용할 수 있습니다")
        left, right = text.replace(".", "|").split("|")
        before, after = parse_moves(left), parse_moves(right)
        if explicit_boundary is not None and explicit_boundary != len(before):
            raise ValueError("--boundary와 수순에 표시된 Phase separator 위치가 다릅니다")
        return before + after, len(before)
    if explicit_boundary is None:
        raise ValueError("--boundary를 지정하거나 --solution에 | 로 Phase 경계를 표시하세요")
    return parse_moves(text), explicit_boundary


def attempt_record(attempt):
    return {
        "kind": attempt.kind, "window_size": attempt.window_size,
        "start": attempt.start, "end": attempt.end,
        "before": format_moves(attempt.before),
        "replacement": (format_moves(attempt.replacement)
                        if attempt.replacement is not None else None),
        "status": attempt.status,
        "local_optimality_proven": attempt.status in ("FOUND", "EXHAUSTED"),
        "elapsed_ms": attempt.elapsed_ms, "generated": attempt.generated,
        "states": attempt.states,
        "solution_length_before": attempt.solution_length_before,
        "solution_length_after": attempt.solution_length_after,
    }


def trace_attempt(attempt):
    shortened = (f" -> {len(attempt.replacement)} HTM" if attempt.replacement is not None else "")
    print(f"[{attempt.kind}] window={attempt.window_size} "
          f"moves={attempt.start + 1}~{attempt.end}: {attempt.status}{shortened}, "
          f"{attempt.elapsed_ms:.3f} ms, {attempt.states} states", file=sys.stderr, flush=True)


def run(args):
    wall_start = time.monotonic()
    if args.command == "init-twophase":
        print("hkociemba 참고 테이블 생성: 약 80 MB, 30분 이상 걸릴 수 있습니다.", flush=True)
        elapsed = prepare_reference(args.table_dir, initialize=True)
        print(f"테이블 준비 완료: {args.table_dir.resolve()} ({elapsed:.1f}초)")
        return
    config = RepairConfig(tuple(args.window), args.time_ms, args.per_window_ms,
                          tuple(args.shifts), args.sliding, args.max_states)
    initial = _source_state(args)
    scramble = parse_moves(args.scramble) if args.scramble is not None else None
    scramble_seed = None
    input_type = ("scramble" if args.scramble is not None else
                  "facelets" if args.facelets is not None else "provided_solution")
    base = None
    if args.command == "solve":
        if initial is None:
            scramble_seed = args.seed if args.seed is not None else secrets.randbits(64)
            length = args.scramble_length if args.scramble_length is not None else 20
            scramble = random_scramble(length, rng=random.Random(scramble_seed))
            initial = transform(scramble)
            input_type = "random_scramble"
            message = (f"자동 스크램블 ({len(scramble)} HTM, seed={scramble_seed}): "
                       f"{format_moves(scramble)}")
            if not args.json:
                print(message, flush=True)
            elif args.trace:
                print(message, file=sys.stderr, flush=True)
        elif args.seed is not None or args.scramble_length is not None:
            raise ValueError("--seed와 --scramble-length는 자동 scramble 생성에만 사용합니다")
        if args.trace:
            print(f"Two-Phase 초기 해 탐색: backend={args.backend}", file=sys.stderr, flush=True)
        base = initial_solution(initial, args.backend, args.base_seconds,
                                args.max_length, args.table_dir)
        moves, boundary = base.moves, base.boundary
    else:
        moves, boundary = _existing_solution(args.solution, args.boundary)
    result = repair_solution(moves, boundary, config, initial,
                             trace_attempt if args.trace else None)
    report = {
        "metric": "HTM", "backend": base.backend if base else "provided_solution",
        "input_type": input_type,
        "scramble": format_moves(scramble) if scramble is not None else None,
        "scramble_length": len(scramble) if scramble is not None else None,
        "seed": scramble_seed,
        "windows": sorted(set(config.windows)), "time_limit_ms": config.time_limit_ms,
        "per_window_ms": config.per_window_ms, "shifts": list(config.shifts),
        "sliding": config.sliding, "max_states": config.max_states,
        "initial_solution": format_moves(result.original),
        "original_boundary": result.original_boundary,
        "initial_phase_left": format_moves(result.original[:boundary]),
        "initial_phase_right": format_moves(result.original[boundary:]),
        "phase2_side": base.phase2_side if base else "user_supplied",
        "phase2_axis": base.axis if base else None,
        "backend_rotation": base.rotation if base else None,
        "backend_inverted": base.inverted if base else None,
        "solution": format_moves(result.moves),
        "initial_length": len(result.original), "final_length": len(result.moves),
        "saved_moves": result.saved_moves, "final_repair_anchor": result.final_anchor,
        "base_elapsed_ms": base.elapsed_ms if base else 0.0,
        "engine_prepare_ms": result.engine_prepare_ms,
        "repair_elapsed_ms": result.elapsed_ms,
        "wall_elapsed_ms": (time.monotonic() - wall_start) * 1000,
        "stop_reason": result.stop_reason, "verified": result.verified,
        "verification_input": "provided_cube" if initial is not None else "inverse_of_provided_solution",
        "global_optimality_proven": False,
        "attempts": [attempt_record(a) for a in result.attempts],
    }
    serialized = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized + "\n", encoding="utf-8")
    if args.json:
        print(serialized)
    else:
        print(f"초기 해 ({len(result.original)} HTM): "
              f"{format_moves(result.original[:boundary])} | {format_moves(result.original[boundary:])}")
        print(f"window={report['windows']}, exact repair 예산={config.time_limit_ms:g} ms")
        if not args.trace:
            for attempt in result.attempts:
                trace_attempt(attempt)
        print(f"최종 해 ({len(result.moves)} HTM): {format_moves(result.moves) or '(이미 solved)'}")
        print(f"단축: {result.saved_moves}수 / 큐브 검증: 통과 / 종료: {result.stop_reason}")
        print(f"Two-Phase {report['base_elapsed_ms']:.3f} ms + "
              f"repair {result.elapsed_ms:.3f} ms "
              f"(엔진 준비 {result.engine_prepare_ms:.3f} ms 별도)")


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        run(args)
    except (ValueError, RuntimeError, OSError) as exc:
        print(f"오류: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("사용자가 탐색을 중단했습니다.", file=sys.stderr)
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
