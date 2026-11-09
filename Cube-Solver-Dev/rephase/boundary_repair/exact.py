"""Python interface to the bounded exact native search."""
import ctypes
from dataclasses import dataclass
from pathlib import Path
import math
import os
import shutil
import subprocess
import tempfile
import threading
import time

from .cube import MOVE_MAPS, transform, validate_state

NATIVE = Path(__file__).parent / "native"
_library = None
_engine_lock = threading.Lock()


class _NativeResult(ctypes.Structure):
    _fields_ = [("status", ctypes.c_int), ("length", ctypes.c_int),
                ("moves", ctypes.c_uint8 * 16), ("generated", ctypes.c_uint64),
                ("states", ctypes.c_uint64)]


@dataclass(frozen=True)
class ExactResult:
    status: str
    moves: tuple[int, ...] | None
    elapsed_ms: float
    generated: int
    states: int


def prepare_engine() -> float:
    """Build/load once, outside the repair budget. Return preparation seconds."""
    if _library is not None:
        return 0.0
    with _engine_lock:
        return _prepare_engine_locked()


def _prepare_engine_locked() -> float:
    global _library
    if _library is not None:
        return 0.0
    start = time.monotonic()
    source = NATIVE / "search.cpp"
    binary = NATIVE / "search.so"
    if not binary.exists() or binary.stat().st_mtime_ns < source.stat().st_mtime_ns:
        compiler = shutil.which("g++") or shutil.which("clang++")
        if not compiler:
            raise RuntimeError("C++ 컴파일러(g++ 또는 clang++)가 필요합니다")
        # Random names also work when separate PID namespaces share this folder.
        fd, temporary_name = tempfile.mkstemp(prefix="search.", suffix=".so", dir=NATIVE)
        os.close(fd)
        temporary = Path(temporary_name)
        try:
            subprocess.run([compiler, "-O3", "-std=c++17", "-shared", "-fPIC",
                            str(source), "-o", str(temporary)],
                           check=True, capture_output=True, text=True, timeout=60)
            temporary.replace(binary)
        except subprocess.CalledProcessError as exc:
            raise RuntimeError(f"탐색 엔진 컴파일 실패:\n{exc.stderr}") from exc
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError("탐색 엔진 컴파일이 60초 제한을 초과했습니다") from exc
        finally:
            temporary.unlink(missing_ok=True)
    library = ctypes.CDLL(str(binary))
    library.br_search.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int,
                                 ctypes.c_uint64, ctypes.c_uint64,
                                 ctypes.POINTER(_NativeResult)]
    library.br_search.restype = None
    _library = library
    return time.monotonic() - start


def exact_search(target: bytes, max_depth: int, time_limit: float,
                 max_states: int = 200_000) -> ExactResult:
    """Find/prove the shortest unrestricted HTM path to target, up to max_depth.

    TIMEOUT/STATE_LIMIT mean incomplete search, never proof of optimality.
    Native setup must be performed before timing a series of repair searches.
    """
    validate_state(target)
    if isinstance(max_depth, bool) or not isinstance(max_depth, int) or not 0 <= max_depth <= 9:
        raise ValueError("max_depth는 0~9 정수여야 합니다")
    if not math.isfinite(time_limit) or not 0 <= time_limit <= 3600:
        raise ValueError("time_limit는 0~3600초의 유한한 값이어야 합니다")
    if isinstance(max_states, bool) or not isinstance(max_states, int) or not 2 <= max_states <= 10_000_000:
        raise ValueError("max_states는 2~10000000 정수여야 합니다")
    prepare_engine()
    result = _NativeResult()
    start = time.monotonic()
    _library.br_search(target, b"".join(MOVE_MAPS), max_depth,
                       int(time_limit * 1_000_000_000), max_states, ctypes.byref(result))
    elapsed_ms = (time.monotonic() - start) * 1000
    status = ("FOUND", "EXHAUSTED", "TIMEOUT", "STATE_LIMIT")[result.status]
    moves = tuple(result.moves[:result.length]) if status == "FOUND" else None
    if moves is not None and (len(moves) > max_depth or transform(moves) != target):
        raise RuntimeError("native 탐색 결과의 변환 검증에 실패했습니다")
    return ExactResult(status, moves, elapsed_ms, result.generated, result.states)
