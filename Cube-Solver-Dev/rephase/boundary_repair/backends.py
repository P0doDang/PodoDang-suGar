"""Two-phase initial solutions with the actual phase cut, not a guessed midpoint."""
import ctypes
from contextlib import redirect_stdout
from dataclasses import dataclass
import importlib
import math
from pathlib import Path
import threading
import time
import sys

from .cube import (FACES, IDENTITY, apply_moves, in_g1, parse_moves,
                   to_facelets, validate_state)

DEFAULT_TABLE_DIR = Path(__file__).resolve().parent.parent / "twophase_tables"
_base_lock = threading.Lock()
_reference_folder = None
_REFERENCE_TABLES = (
    "conj_twist", "conj_ud_edges", "fs_classidx", "fs_sym", "fs_rep",
    "co_classidx", "co_sym", "co_rep", "move_twist", "move_flip",
    "move_slice_sorted", "move_u_edges", "move_d_edges", "move_ud_edges",
    "move_corners", "phase2_edgemerge", "phase1_prun", "phase2_prun",
    "phase2_cornsliceprun",
)


@dataclass(frozen=True)
class InitialSolution:
    moves: tuple[int, ...]
    boundary: int
    backend: str
    elapsed_ms: float
    phase2_side: str = "suffix"
    axis: str = "UD"
    rotation: int = 0
    inverted: bool = False


def _verify(initial: bytes, result: InitialSolution):
    if not 0 <= result.boundary <= len(result.moves):
        raise RuntimeError("backend Phase 경계가 수순 범위를 벗어났습니다")
    if apply_moves(initial, result.moves) != IDENTITY:
        raise RuntimeError("Two-Phase 초기 해가 실제로 큐브를 풀지 못했습니다")
    restricted = (result.moves[result.boundary:] if result.phase2_side == "suffix"
                  else result.moves[:result.boundary])
    if any(m % 3 != 1 and FACES[m // 3] not in result.axis for m in restricted):
        raise RuntimeError("Phase 2 수순이 해당 축의 subgroup move가 아닙니다")
    if result.axis == "UD" and result.phase2_side == "suffix":
        if not in_g1(apply_moves(initial, result.moves[:result.boundary])):
            raise RuntimeError("Phase 1 종료 상태가 G1 subgroup에 속하지 않습니다")


def _solve_kociemba(initial: bytes, timeout: float, max_length: int) -> InitialSolution:
    """kociemba 1.2.1 exports solution(..., useSeparator, ...).

    Its public solve() suppresses the separator and fixes timeout=1000, so use
    the exported native function with useSeparator=1 and free its malloc result.
    This adapter deliberately pins the package ABI in requirements.txt.
    """
    start = time.monotonic()
    try:
        package = importlib.import_module("kociemba")
        wrapper = importlib.import_module("kociemba.ckociembawrapper")
    except ImportError as exc:
        raise RuntimeError("kociemba native backend가 필요합니다: pip install -r requirements.txt") from exc
    library = ctypes.CDLL(wrapper.__file__)
    try:
        function = library.solution
    except AttributeError as exc:
        raise RuntimeError("지원하지 않는 kociemba ABI입니다. kociemba==1.2.1을 사용하세요") from exc
    function.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_long,
                         ctypes.c_int, ctypes.c_char_p]
    function.restype = ctypes.c_void_p
    libc = ctypes.CDLL(None)
    libc.free.argtypes = [ctypes.c_void_p]
    libc.free.restype = None
    with _base_lock:  # upstream pruning-table initialization is process-global
        pointer = function(to_facelets(initial).encode("ascii"), max_length,
                           max(1, math.ceil(timeout)), 1,
                           str(Path(package.__file__).parent / "cprunetables").encode())
        if not pointer:
            raise RuntimeError("초기 해 탐색 실패: base timeout 또는 max-length를 늘리세요")
        try:
            text = ctypes.string_at(pointer).decode("ascii").strip()
        finally:
            libc.free(pointer)
    if text.count(".") != 1:
        raise RuntimeError(f"backend가 실제 Phase separator를 반환하지 않았습니다: {text}")
    phase1, phase2 = (parse_moves(s) for s in text.split("."))
    result = InitialSolution(phase1 + phase2, len(phase1), "kociemba",
                             (time.monotonic() - start) * 1000)
    _verify(initial, result)
    return result


def reference_phase_cut(phase1_length: int, phase2_length: int,
                        inverted: bool) -> tuple[int, str]:
    """Inverse search reverses the entire output, so Phase 2 becomes a prefix."""
    return (phase2_length, "prefix") if inverted else (phase1_length, "suffix")


def prepare_reference(table_dir: Path = DEFAULT_TABLE_DIR, *, initialize=False) -> float:
    """Reference tables are expensive: only explicitly initialize on request."""
    global _reference_folder
    start = time.monotonic()
    table_dir = Path(table_dir).resolve()
    if _reference_folder is not None:
        if _reference_folder != table_dir:
            raise RuntimeError("한 Python 프로세스에서 reference table-dir를 바꿀 수 없습니다")
        return 0.0
    missing = [name for name in _REFERENCE_TABLES if not (table_dir / name).is_file()]
    if missing and not initialize:
        raise RuntimeError(
            f"hkociemba reference 테이블이 준비되지 않았습니다 ({len(missing)}개 없음).\n"
            "먼저 python solve.py init-twophase 를 실행하세요.\n"
            "최초 생성은 약 80 MB, 하드웨어에 따라 30분 이상 걸릴 수 있습니다.\n"
            "즉시 실행하려면 기본 --backend kociemba 를 사용하세요.")
    try:
        defs = importlib.import_module("twophase.defs")
    except ImportError as exc:
        raise RuntimeError("reference backend가 필요합니다: pip install -r requirements-reference.txt") from exc
    # All downstream modules import this value. Never change working directory.
    table_dir.mkdir(parents=True, exist_ok=True)
    defs.FOLDER = str(table_dir)
    # Upstream prints table-loading progress; keep machine-readable stdout clean.
    with redirect_stdout(sys.stderr):
        importlib.import_module("twophase.solver")
    _reference_folder = table_dir
    return time.monotonic() - start


def _solve_reference(initial: bytes, timeout: float, max_length: int,
                     table_dir: Path) -> InitialSolution:
    prepare_reference(table_dir)
    sv = importlib.import_module("twophase.solver")
    sy = importlib.import_module("twophase.symmetries")
    face = importlib.import_module("twophase.face")
    fc = face.FaceCube()
    if fc.from_string(to_facelets(initial)) is not True:
        raise RuntimeError("reference backend에서 facelets를 읽지 못했습니다")
    cc = fc.to_cubie_cube()
    start = time.monotonic()
    shortest = [999]
    terminated = threading.Event()
    shared_lock = threading.Lock()

    class RecordedSolutions(list):
        def __init__(self):
            super().__init__()
            self.records = []

        def append(self, moves):
            # Called while the original SolverThread still holds its phase paths.
            thread = threading.current_thread()
            boundary, side = reference_phase_cut(len(thread.sofar_phase1),
                                                 len(thread.sofar_phase2), bool(thread.inv))
            axis_move = int(sy.conj_move[18 * 16 * thread.rot])  # conjugated U1
            axis = ("UD", "RL", "FB")[axis_move // 3 % 3]
            self.records.append(InitialSolution(tuple(int(m) for m in moves), boundary,
                                "twophase", 0.0, side, axis, thread.rot, bool(thread.inv)))
            super().append(moves)

    solutions = RecordedSolutions()
    symmetries = set(cc.symmetries())
    directions = [0, 3] if symmetries & {16, 20, 24, 28} else list(range(6))
    if symmetries & set(range(48, 96)):
        directions = [i for i in directions if i < 3]
    errors = []

    class CapturedThread(sv.SolverThread):
        def run(self):
            try:
                super().run()
            except Exception as exc:
                errors.append(exc)
                terminated.set()

    threads = []
    for i in directions:
        thread = CapturedThread(cc, i % 3, i // 3, max_length, timeout, start,
                                solutions, terminated, shortest)
        thread.lock = shared_lock
        threads.append(thread)
        thread.start()
    for thread in threads:
        thread.join()
    if errors:
        raise RuntimeError(f"reference 탐색 오류: {errors[0]}") from errors[0]
    if not solutions.records:
        raise RuntimeError("reference 탐색에서 해를 찾지 못했습니다")
    recorded = min(solutions.records, key=lambda r: len(r.moves))
    result = InitialSolution(recorded.moves, recorded.boundary, recorded.backend,
                             (time.monotonic() - start) * 1000, recorded.phase2_side,
                             recorded.axis, recorded.rotation, recorded.inverted)
    _verify(initial, result)
    return result


def initial_solution(initial: bytes, backend="kociemba", timeout=3.0,
                     max_length=24, table_dir: Path = DEFAULT_TABLE_DIR) -> InitialSolution:
    validate_state(initial)
    if not math.isfinite(timeout) or not 0 < timeout <= 3600:
        raise ValueError("base timeout는 0초 초과~3600초의 유한한 값이어야 합니다")
    if isinstance(max_length, bool) or not isinstance(max_length, int) or not 1 <= max_length <= 30:
        raise ValueError("max_length는 1~30 정수여야 합니다")
    if backend not in ("kociemba", "twophase"):
        raise ValueError(f"알 수 없는 backend: {backend}")
    if initial == IDENTITY:
        return InitialSolution((), 0, backend, 0.0)
    if backend == "kociemba":
        return _solve_kociemba(initial, timeout, max_length)
    return _solve_reference(initial, timeout, max_length, table_dir)
