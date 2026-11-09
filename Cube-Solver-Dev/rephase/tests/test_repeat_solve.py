"""New runner checks: native phase attribution, output filtering and isolation."""
from contextlib import redirect_stderr, redirect_stdout
import io
import json
import multiprocessing as mp
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from boundary_repair import RepairConfig, initial_solution, parse_moves, transform
from boundary_repair.cube import IDENTITY, apply_moves
import repeat_solve
from repeat_metrics.profiler import ProfiledSolver, build_profiler

ROOT = Path(__file__).resolve().parent.parent


class RepeatRunnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = build_profiler()

    def options(self):
        return {"index": 1, "seed": 123, "binary": str(self.binary),
                "config": RepairConfig((6, 8), 100)}

    def test_actual_phase_metrics_match_original_solution(self):
        state = transform(parse_moves("R U R' U' F2 D L2 B R2 U F'"))
        profiler = ProfiledSolver(self.binary)
        profiler.start_monitor(0.2)
        try:
            measured = profiler.solve(state)
        finally:
            metrics = profiler.stop_monitor()
        original = initial_solution(state)
        self.assertEqual(measured.moves, original.moves)
        self.assertEqual(measured.boundary, original.boundary)
        self.assertEqual(metrics["phase1_ns"] + metrics["phase2_ns"], metrics["total_ns"])
        self.assertGreater(metrics["phase1_ns"], 0)
        self.assertGreater(metrics["phase2_ns"], 0)
        self.assertGreater(metrics["phase2_calls"], 1)
        self.assertGreater(metrics["phase1_rss"], 0)
        self.assertGreater(metrics["phase2_rss"], 0)
        self.assertGreater(metrics["phase1_samples"], 0)
        self.assertGreater(metrics["phase2_samples"], 0)

    def test_trial_metrics_include_repair_and_verify_cube(self):
        record = repeat_solve.measure_trial(**self.options())
        self.assertTrue(record["verified"])
        self.assertEqual(record["phase1_moves"] + record["phase2_moves"], record["initial_total_moves"])
        self.assertEqual(record["saved_moves"], record["initial_total_moves"] - record["final_total_moves"])
        self.assertEqual(apply_moves(transform(parse_moves(record["scramble"])),
                                     parse_moves(record["solution"])), IDENTITY)
        self.assertAlmostEqual(record["phase1_ms"] + record["phase2_ms"], record["twophase_ms"])
        self.assertAlmostEqual(record["twophase_ms"] + record["repair_exact_ms"], record["pure_solve_ms"])
        self.assertAlmostEqual(record["repair_exact_ms"], sum(a["elapsed_ms"] for a in record["attempts"]))
        memory = record["memory"]
        self.assertGreater(memory["repair_peak_rss_bytes"], 0)
        peaks = [memory[k] or 0 for k in ("phase1_peak_rss_bytes", "phase2_peak_rss_bytes",
                                         "repair_peak_rss_bytes")]
        self.assertEqual(memory["total_peak_rss_bytes"], max(peaks))

    def test_output_filter_only_verified_improvements(self):
        # Output-only unit fixtures, not actual solve results or benchmark data.
        stdout, saved = io.StringIO(), io.StringIO()
        fixtures = [{"verified": True, "saved_moves": 0},
                    {"verified": False, "saved_moves": 2},
                    {"verified": True, "saved_moves": 1}]
        emitted = [repeat_solve.emit_hit(r, as_json=True, output=saved, stream=stdout) for r in fixtures]
        self.assertEqual(emitted, [False, False, True])
        self.assertEqual([json.loads(line) for line in stdout.getvalue().splitlines()], [fixtures[2]])
        self.assertEqual(saved.getvalue(), stdout.getvalue())

    def test_human_hit_output_includes_requested_metrics(self):
        # Synthetic formatting fixture: no measured time/memory is invented.
        fixture = {
            "trial": 1, "seed": 0, "saved_moves": 1, "verified": True,
            "scramble": "OUTPUT_FORMAT_FIXTURE", "initial_total_moves": 3,
            "phase1_solution": "R", "phase2_solution": "U U", "solution": "R U2",
            "phase1_moves": 1, "phase2_moves": 2, "final_total_moves": 2,
            "phase1_ms": 0.0, "phase2_ms": 0.0, "twophase_ms": 0.0,
            "repair_exact_ms": 0.0, "pure_solve_ms": 0.0, "phase2_search_calls": 0,
            "preparation_ms": 0.0, "repair_stage_ms": 0.0, "trial_wall_ms": 0.0,
            "stop_reason": "OUTPUT_FORMAT_FIXTURE", "attempts": [],
            "memory": {key: None for key in ("phase1_peak_rss_bytes", "phase2_peak_rss_bytes",
                                              "repair_peak_rss_bytes", "total_peak_rss_bytes")},
        }
        output = io.StringIO()
        self.assertTrue(repeat_solve.emit_hit(fixture, stream=output))
        for label in ("1수 단축", "Phase 1", "Phase 2", "Exact repair", "전체 solve",
                      "탐색 시간(ms)", "수순 수(HTM)", "RSS(MiB)", "최종 해 (2 HTM)"):
            self.assertIn(label, output.getvalue())
        self.assertIn("미관측", output.getvalue())

    def test_stop_after_hits_and_seed_sequence(self):
        args = repeat_solve.build_parser().parse_args([
            "--count", "10", "--seed", "500", "--stop-after-hits", "1", "--json"])
        # Unit-test the controller separately from genuine native measurements.
        with (patch.object(repeat_solve, "isolated_trial", side_effect=[
                {"verified": True, "saved_moves": 0},
                {"verified": True, "saved_moves": 1, "attempts": [{"status": "FOUND"}]}]) as trial,
             patch.object(repeat_solve, "build_profiler", return_value=self.binary),
              redirect_stdout(io.StringIO()) as stdout, redirect_stderr(io.StringIO()) as stderr):
            self.assertEqual(repeat_solve.run(args), 0)
        self.assertEqual(trial.call_count, 2)
        self.assertEqual([c.args[0]["seed"] for c in trial.call_args_list], [500, 501])
        self.assertEqual(len(stdout.getvalue().splitlines()), 1)
        self.assertIn("1수 이상 단축 1개", stderr.getvalue())
        self.assertIn("시행 #2: 1수 단축 (window 교체 1회)", stderr.getvalue())
        self.assertIn("총 단축: 1수 / window 교체: 1회", stderr.getvalue())

    def test_cli_repeats_without_outputting_nonhits(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "hits.jsonl"
            process = subprocess.run([sys.executable, str(ROOT / "repeat_solve.py"),
                                      "--count", "2", "--seed", "123", "--window", "6",
                                      "--time-ms", "20", "--json", "--output", str(output)],
                                     capture_output=True, text=True, timeout=30)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertIn("2회 검증", process.stderr)
            if not process.stdout:
                self.assertIn("단축이 발생한 시행: 없음", process.stderr)
            for line in process.stdout.splitlines():
                record = json.loads(line)
                self.assertTrue(record["verified"])
                self.assertGreaterEqual(record["saved_moves"], 1)
                self.assertGreater(record["memory"]["total_peak_rss_bytes"], 0)
                self.assertTrue(record["memory"]["isolated_process_per_trial"])
            self.assertEqual(output.read_text(), process.stdout)

    def test_parent_timeout_reaps_worker(self):
        before = {p.pid for p in mp.active_children()}
        with self.assertRaisesRegex(RuntimeError, "안전 한도"):
            repeat_solve.isolated_trial(self.options(), 0.001)
        self.assertEqual({p.pid for p in mp.active_children()}, before)


if __name__ == "__main__":
    unittest.main()
