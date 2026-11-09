"""Budgeted phase-boundary repair and optional sliding-window repair."""
from dataclasses import dataclass, field
import math
import time

from .cube import (IDENTITY, apply_moves, inverse, transform,
                   validate_state)
from .exact import exact_search, prepare_engine


@dataclass(frozen=True)
class RepairConfig:
    windows: tuple[int, ...] = (6, 8)
    time_limit_ms: float = 100.0  # ONE shared budget for the complete repair stage
    per_window_ms: float | None = None
    shifts: tuple[int, ...] = (0,)
    sliding: bool = False
    max_states: int = 200_000

    def __post_init__(self):
        if not self.windows or any(isinstance(w, bool) or not isinstance(w, int)
                                   or not 2 <= w <= 10 for w in self.windows):
            raise ValueError("windows는 2~10 정수 목록이어야 합니다")
        if not math.isfinite(self.time_limit_ms) or not 0 <= self.time_limit_ms <= 3_600_000:
            raise ValueError("time_limit_ms는 0~3600000의 유한한 값이어야 합니다")
        if self.per_window_ms is not None and (not math.isfinite(self.per_window_ms)
                                               or not 0 < self.per_window_ms <= 3_600_000):
            raise ValueError("per_window_ms는 0 초과~3600000의 유한한 값이어야 합니다")
        if not self.shifts or any(isinstance(s, bool) or not isinstance(s, int) for s in self.shifts):
            raise ValueError("shifts는 정수 목록이어야 합니다")
        if isinstance(self.max_states, bool) or not isinstance(self.max_states, int) or not 2 <= self.max_states <= 10_000_000:
            raise ValueError("max_states는 2~10000000 정수여야 합니다")


@dataclass(frozen=True)
class WindowAttempt:
    kind: str
    window_size: int
    start: int  # zero-based half-open indices in the solution at this attempt
    end: int
    before: tuple[int, ...]
    replacement: tuple[int, ...] | None
    status: str
    elapsed_ms: float
    generated: int
    states: int
    solution_length_before: int
    solution_length_after: int


@dataclass(frozen=True)
class RepairResult:
    original: tuple[int, ...]
    moves: tuple[int, ...]
    original_boundary: int
    final_anchor: int  # repair-region anchor; unrestricted replacements may leave G1
    elapsed_ms: float
    engine_prepare_ms: float
    stop_reason: str
    verified: bool
    attempts: tuple[WindowAttempt, ...] = field(default_factory=tuple)

    @property
    def saved_moves(self):
        return len(self.original) - len(self.moves)


def boundary_windows(length: int, boundary: int, size: int, shifts=(0,)):
    """Full-sized centered windows, adjusted at endpoints; interior cuts cross both phases."""
    if length < size:
        return
    seen = set()
    for shift in sorted(set(shifts), key=lambda s: (abs(s), s)):
        start = min(max(boundary - size // 2 + shift, 0), length - size)
        end = start + size
        if 0 < boundary < length and not start < boundary < end:
            continue
        if start not in seen:
            seen.add(start)
            yield start, end


def _normalize_with_anchor(moves, anchor):
    stack = []
    for i, move in enumerate(moves):
        before = i < anchor
        if stack and stack[-1][0] // 3 == move // 3:
            previous, previous_before = stack.pop()
            power = (previous % 3 + move % 3 + 2) % 4
            if power:
                stack.append((3 * (move // 3) + power - 1, previous_before and before))
        else:
            stack.append((move, before))
    return tuple(m for m, _ in stack), sum(before for _, before in stack)


def repair_solution(moves, boundary: int, config: RepairConfig | None = None,
                    initial_state: bytes | None = None, on_attempt=None) -> RepairResult:
    config = config or RepairConfig()
    original = tuple(moves)
    if any(isinstance(m, bool) or not isinstance(m, int) or not 0 <= m < 18 for m in original):
        raise ValueError("moves는 0~17 move ID 목록이어야 합니다")
    if isinstance(boundary, bool) or not isinstance(boundary, int) or not 0 <= boundary <= len(original):
        raise ValueError("boundary는 0~수순 길이 사이의 정수여야 합니다")
    original_transform = transform(original)
    initial = inverse(original_transform) if initial_state is None else initial_state
    validate_state(initial)
    if apply_moves(initial, original) != IDENTITY:
        raise ValueError("입력한 초기 수순이 주어진 큐브를 풀지 못합니다")
    preparation_ms = prepare_engine() * 1000 if config.time_limit_ms > 0 else 0.0
    start_time = time.monotonic()
    deadline = start_time + config.time_limit_ms / 1000
    current, anchor = original, boundary
    attempts = []
    seen = set()
    stop_reason = "NO_IMPROVEMENT"
    if not any(w <= len(current) for w in config.windows):
        stop_reason = "NO_WINDOW"
    # Improvement strictly decreases length, so the restart loop is finite.
    while any(w <= len(current) for w in config.windows):
        improved = False
        candidates = []
        for size in sorted(set(config.windows)):
            candidates.extend(("boundary", size, a, b) for a, b in
                              boundary_windows(len(current), anchor, size, config.shifts))
        if config.sliding:
            for size in sorted(set(config.windows)):
                candidates.extend(("sliding", size, a, a + size)
                                  for a in range(len(current) - size + 1))
        for kind, size, a, b in candidates:
            window = current[a:b]
            key = window  # the exact delta depends only on the move transformation
            if key in seen:
                continue
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                stop_reason = "TIME_BUDGET"
                break
            seen.add(key)
            limit = remaining
            if config.per_window_ms is not None:
                limit = min(limit, config.per_window_ms / 1000)
            # T = A^-1 * B = transformation(window). Search Id -> T with all 18 moves.
            exact = exact_search(transform(window), size - 1, limit, config.max_states)
            old_length = len(current)
            if exact.moves is not None:
                replacement = exact.moves
                if transform(replacement) != transform(window):
                    raise RuntimeError("window 변환 불일치")
                new_moves = current[:a] + replacement + current[b:]
                if anchor <= a:
                    new_anchor = anchor
                elif anchor >= b:
                    new_anchor = anchor + len(replacement) - size
                else:
                    new_anchor = a + min(anchor - a, len(replacement))
                new_moves, new_anchor = _normalize_with_anchor(new_moves, new_anchor)
                if len(new_moves) >= len(current) or transform(new_moves) != original_transform:
                    raise RuntimeError("repair 교체의 전체 변환 또는 길이 검증 실패")
                current, anchor = new_moves, new_anchor
                improved = True
            attempt = WindowAttempt(kind, size, a, b, window, exact.moves, exact.status,
                                    exact.elapsed_ms, exact.generated, exact.states,
                                    old_length, len(current))
            attempts.append(attempt)
            if on_attempt is not None:
                on_attempt(attempt)
            if time.monotonic() >= deadline:
                stop_reason = "TIME_BUDGET"
                break
            if improved:
                break  # regenerate windows around the repaired region
        if stop_reason == "TIME_BUDGET":
            break
        if not improved:
            stop_reason = ("INCOMPLETE_SEARCH" if any(a.status in ("TIMEOUT", "STATE_LIMIT")
                                                     for a in attempts) else "NO_IMPROVEMENT")
            break
        if not any(w <= len(current) for w in config.windows):
            stop_reason = "NO_WINDOW"
    elapsed_ms = (time.monotonic() - start_time) * 1000
    if len(current) > len(original) or apply_moves(initial, current) != IDENTITY:
        raise RuntimeError("최종 solution 검증 실패")
    return RepairResult(original, current, boundary, anchor, elapsed_ms,
                        preparation_ms, stop_reason, True, tuple(attempts))
