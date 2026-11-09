"""Build the new runner's native instrumentation without changing old code."""
import ctypes
from dataclasses import dataclass
import hashlib
from importlib.metadata import version
import importlib
import math
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

from boundary_repair.backends import InitialSolution, _verify
from boundary_repair.cube import IDENTITY, parse_moves, to_facelets

NATIVE = Path(__file__).parent / "native"
FIELDS = ("total_ns", "phase1_ns", "phase2_ns", "phase2_calls", "baseline_rss",
          "phase1_rss", "phase2_rss", "repair_rss", "total_rss", "phase1_samples",
          "phase2_samples", "repair_samples", "interval_ns")


class NativeMetrics(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in FIELDS]


def build_profiler() -> Path:
    try:
        package = importlib.import_module("kociemba")
        if version("kociemba") != "1.2.1":
            raise RuntimeError("repeat profiler는 kociemba==1.2.1이 필요합니다")
    except ImportError as exc:
        raise RuntimeError("먼저 .venv/bin/python -m pip install -r requirements.txt 를 실행하세요") from exc
    upstream = Path(package.__file__).parent / "ckociemba"
    source_path = upstream / "search.c"
    source = source_path.read_text()
    signature = "int totalDepth(search_t* search, int depthPhase1, int maxDepth)"
    if source.count(signature) != 1:
        raise RuntimeError("지원하지 않는 upstream search.c입니다; 계측을 중단합니다")
    instrumented = source.replace(signature,
        "int brp_phase2_impl(search_t* search, int depthPhase1, int maxDepth)", 1)
    dependencies = [upstream / name for name in
                    ("coordcube.c", "cubiecube.c", "facecube.c", "prunetable_helpers.c")]
    digest = hashlib.sha256()
    digest.update(b"repeat-profiler-v1-bsymbolic")
    digest.update(platform.machine().encode())
    for path in [NATIVE / "profile.c", source_path, *dependencies,
                 *sorted((upstream / "include").glob("*.h"))]:
        digest.update(path.name.encode())
        digest.update(path.read_bytes())
    binary = NATIVE / f"profiler.{digest.hexdigest()[:20]}.so"
    if binary.exists():
        return binary.resolve()
    compiler = shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        raise RuntimeError("repeat profiler 빌드에는 gcc 또는 clang이 필요합니다")
    with tempfile.TemporaryDirectory(prefix="profile-build-", dir=NATIVE) as directory:
        temporary = Path(directory)
        (temporary / "search_profiled.inc").write_text(instrumented)
        output = temporary / "profiler.so"
        command = [compiler, "-O3", "-std=c11", "-D_XOPEN_SOURCE=700", "-shared", "-fPIC",
                   "-pthread", "-Wl,-Bsymbolic", "-I", str(upstream / "include"), "-I", str(temporary),
                   str(NATIVE / "profile.c"), *map(str, dependencies), "-o", str(output)]
        try:
            subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
        except subprocess.CalledProcessError as exc:
            raise RuntimeError(f"새 profiler 빌드 실패:\n{exc.stderr}") from exc
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError("새 profiler 빌드가 60초 제한을 초과했습니다") from exc
        output.replace(binary)
    return binary.resolve()


class ProfiledSolver:
    def __init__(self, binary: Path):
        package = importlib.import_module("kociemba")
        self.cache_dir = Path(package.__file__).parent / "cprunetables"
        # Never regenerate tables in the installed package.
        required = ("twistMove", "flipMove", "FRtoBR_Move", "URFtoDLF_Move", "URtoDF_Move",
                    "URtoUL_Move", "UBtoDF_Move", "MergeURtoULandUBtoDF",
                    "Slice_URFtoDLF_Parity_Prun", "Slice_URtoDF_Parity_Prun",
                    "Slice_Twist_Prun", "Slice_Flip_Prun")
        missing = [name for name in required if not (self.cache_dir / name).is_file()]
        if missing:
            raise RuntimeError(f"기존 kociemba 테이블이 없습니다: {missing}")
        self.lib = ctypes.CDLL(str(binary))
        self.lib.brp_prepare.argtypes = [ctypes.c_char_p]
        self.lib.brp_prepare.restype = None
        self.lib.brp_solve.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_long, ctypes.c_char_p]
        self.lib.brp_solve.restype = ctypes.c_void_p
        self.lib.brp_start_monitor.argtypes = [ctypes.c_uint64]
        self.lib.brp_start_monitor.restype = ctypes.c_int
        self.lib.brp_stop_monitor.argtypes = [ctypes.POINTER(NativeMetrics)]
        self.lib.brp_stop_monitor.restype = None
        self.lib.brp_set_phase.argtypes = [ctypes.c_int]
        self.lib.brp_set_phase.restype = None
        self.lib.brp_end_phase.argtypes = []
        self.lib.brp_end_phase.restype = None
        self.lib.brp_prepare(str(self.cache_dir).encode())
        self.libc = ctypes.CDLL(None)
        self.libc.free.argtypes = [ctypes.c_void_p]
        self.libc.free.restype = None
        self.monitoring = False

    def start_monitor(self, sample_ms: float):
        if not math.isfinite(sample_ms) or not 0.1 <= sample_ms <= 100:
            raise ValueError("sample-ms는 0.1~100의 유한한 값이어야 합니다")
        status = self.lib.brp_start_monitor(int(sample_ms * 1_000_000))
        if status:
            raise RuntimeError(f"RSS 관측 시작 실패: {status}")
        self.monitoring = True

    def solve(self, initial: bytes, timeout: float = 3, max_length: int = 24) -> InitialSolution:
        if not self.monitoring:
            raise RuntimeError("solve 전에 start_monitor를 호출해야 합니다")
        if not math.isfinite(timeout) or not 0 < timeout <= 3600 or not 1 <= max_length <= 30:
            raise ValueError("base timeout 또는 max-length 범위가 잘못되었습니다")
        if initial == IDENTITY:
            return InitialSolution((), 0, "profiled_kociemba", 0.0)
        pointer = self.lib.brp_solve(to_facelets(initial).encode(), max_length,
                                    max(1, math.ceil(timeout)), str(self.cache_dir).encode())
        if not pointer:
            raise RuntimeError("초기 해 실패: base-seconds 또는 max-length를 늘리세요")
        try:
            text = ctypes.string_at(pointer).decode().strip()
        finally:
            self.libc.free(pointer)
        if text.count(".") != 1:
            raise RuntimeError("계측 backend가 실제 Phase separator를 반환하지 않았습니다")
        phase1, phase2 = (parse_moves(s) for s in text.split("."))
        result = InitialSolution(phase1 + phase2, len(phase1), "profiled_kociemba", 0.0)
        _verify(initial, result)
        return result

    def begin_repair(self):
        self.lib.brp_set_phase(3)

    def end_repair(self):
        self.lib.brp_end_phase()

    def stop_monitor(self) -> dict[str, int]:
        if not self.monitoring:
            raise RuntimeError("RSS 관측이 실행 중이 아닙니다")
        output = NativeMetrics()
        self.lib.brp_stop_monitor(ctypes.byref(output))
        self.monitoring = False
        return {name: int(getattr(output, name)) for name in FIELDS}
