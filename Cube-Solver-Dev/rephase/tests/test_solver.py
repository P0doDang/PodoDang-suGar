"""Independent graph-distance, cube-oracle, budget and end-to-end checks."""
from collections import deque
import json
from pathlib import Path
import random
import subprocess
import sys
import unittest

from boundary_repair import (RepairConfig, exact_search, initial_solution,
                             parse_moves, prepare_engine, repair_solution)
from boundary_repair.backends import prepare_reference, reference_phase_cut
from boundary_repair.cube import (FACES, IDENTITY, MOVE_MAPS, SOLVED_FACELETS,
                                 apply_moves, compose, format_moves, from_facelets,
                                 in_g1, inverse, inverse_moves, normalize, to_facelets,
                                 transform)
from boundary_repair.repair import boundary_windows

ROOT = Path(__file__).resolve().parent.parent


def independent_distances(max_depth):
    """Unidirectional Python BFS; no native search or bidirectional bookkeeping."""
    distances = {IDENTITY: 0}
    queue = deque([IDENTITY])
    while queue:
        state = queue.popleft()
        depth = distances[state]
        if depth == max_depth:
            continue
        for move in MOVE_MAPS:
            child = compose(state, move)
            if child not in distances:
                distances[child] = depth + 1
                queue.append(child)
    return distances


class CubeTests(unittest.TestCase):
    def test_inverse_and_facelets(self):
        rng = random.Random(7)
        self.assertEqual(from_facelets(SOLVED_FACELETS), IDENTITY)
        for _ in range(40):
            moves = tuple(rng.randrange(18) for _ in range(20))
            state = transform(moves)
            self.assertEqual(compose(state, inverse(state)), IDENTITY)
            self.assertEqual(compose(inverse(state), state), IDENTITY)
            self.assertEqual(transform(inverse_moves(moves)), inverse(state))
            self.assertEqual(from_facelets(to_facelets(state)), state)
            self.assertEqual(transform(normalize(moves)), state)

    def test_reference_cubie_oracle_without_expensive_tables(self):
        try:
            from twophase.cubie import CubieCube, basicMoveCube
        except ImportError:
            self.skipTest("optional RubikTwoPhase package not installed")
        rng = random.Random(11)
        for _ in range(30):
            moves = tuple(rng.randrange(18) for _ in range(20))
            reference = CubieCube()
            for m in moves:
                for _ in range(m % 3 + 1):
                    reference.multiply(basicMoveCube[m // 3])
            packed = bytes([3 * int(p) + o for p, o in zip(reference.cp, reference.co)] +
                           [2 * int(p) + o for p, o in zip(reference.ep, reference.eo)])
            self.assertEqual(transform(moves), packed)
            self.assertEqual(to_facelets(packed), reference.to_facelet_cube().to_string())

    def test_invalid_input(self):
        for text in ("R4", "X", "r", "R2'", "R (19f) junk"):
            with self.assertRaises(ValueError):
                parse_moves(text)
        with self.assertRaises(ValueError):
            from_facelets("U" * 54)
        flipped = list(SOLVED_FACELETS)
        flipped[5], flipped[10] = flipped[10], flipped[5]
        with self.assertRaises(ValueError):
            from_facelets("".join(flipped))


class ExactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        prepare_engine()
        cls.distances = independent_distances(3)

    def test_against_independent_bfs(self):
        self.assertEqual(len(self.distances), 3502)
        rng = random.Random(17)
        shallow = [s for s, d in self.distances.items() if d <= 2]
        depth_three = rng.sample([s for s, d in self.distances.items() if d == 3], 40)
        for state in shallow + depth_three:
            expected = self.distances[state]
            result = exact_search(state, 7, 1)
            self.assertEqual(result.status, "FOUND")
            self.assertEqual(len(result.moves), expected)
            self.assertEqual(transform(result.moves), state)
            if expected:
                shorter = exact_search(state, expected - 1, 1)
                self.assertEqual(shorter.status, "EXHAUSTED")

    def test_even_odd_paths_and_exhaustion(self):
        for text in ("R U F D", "R U F D L", "R U F D L B", "R U F D L B R"):
            moves = parse_moves(text)
            state = transform(moves)
            result = exact_search(state, len(moves), 1)
            self.assertEqual(result.status, "FOUND")
            self.assertEqual(transform(result.moves), state)
            self.assertEqual(exact_search(state, len(result.moves) - 1, 1).status, "EXHAUSTED")

    def test_timeout_and_state_limit(self):
        state = transform(parse_moves("R U F D L B"))
        timeout = exact_search(state, 7, 0)
        self.assertEqual(timeout.status, "TIMEOUT")
        self.assertIsNone(timeout.moves)
        capped = exact_search(state, 7, 1, max_states=2)
        self.assertEqual(capped.status, "STATE_LIMIT")
        self.assertLessEqual(capped.states, 2)
        for depth, seconds, states in ((10, 1, 200000), (7, -1, 200000),
                                      (7, float("nan"), 200000), (7, 1, 1)):
            with self.assertRaises(ValueError):
                exact_search(state, depth, seconds, states)


class RepairTests(unittest.TestCase):
    def test_six_and_eight_windows(self):
        # No adjacent same-face moves: exact graph search must detect the
        # commuting U/D detour. The supplied cut has a valid UD Phase-2 suffix.
        for text, size, shortest in (("R U D U' D' F2", 6, 2),
                                     ("R U D U' D' F2 L2 B2", 8, 4)):
            moves = parse_moves(text)
            initial = inverse(transform(moves))
            self.assertTrue(in_g1(apply_moves(initial, moves[:3])))
            self.assertEqual(normalize(moves), moves)
            result = repair_solution(moves, 3, RepairConfig((size,), 100), initial)
            self.assertEqual(len(result.moves), shortest)
            self.assertEqual(apply_moves(initial, result.moves), IDENTITY)
            self.assertEqual(result.attempts[0].window_size, size)
            self.assertEqual(result.attempts[0].status, "FOUND")

    def test_budget_fallback(self):
        moves = parse_moves("R U F D L B R U")
        result = repair_solution(moves, 4, RepairConfig((6, 8), 0))
        self.assertEqual(result.moves, moves)
        self.assertEqual(result.stop_reason, "TIME_BUDGET")
        self.assertEqual(len(result.attempts), 0)
        result = repair_solution(moves, 4, RepairConfig((8,), 10, max_states=2))
        self.assertEqual(result.moves, moves)
        self.assertEqual(result.stop_reason, "INCOMPLETE_SEARCH")

    def test_window_bounds(self):
        self.assertEqual(list(boundary_windows(20, 10, 6)), [(7, 13)])
        self.assertEqual(list(boundary_windows(20, 10, 8)), [(6, 14)])
        self.assertEqual(list(boundary_windows(20, 1, 8)), [(0, 8)])
        self.assertEqual(list(boundary_windows(20, 19, 8)), [(12, 20)])
        self.assertEqual(list(boundary_windows(5, 2, 6)), [])
        for boundary in range(1, 20):
            for a, b in boundary_windows(20, boundary, 8, (-8, -1, 0, 1, 8)):
                self.assertLess(a, boundary)
                self.assertGreater(b, boundary)
                self.assertEqual(b - a, 8)

    def test_shifts_sliding_and_restarts_preserve_transformation(self):
        moves = parse_moves("R U D U' D' F2 L2 B2 R U D U' D' F2")
        result = repair_solution(moves, 7, RepairConfig((8, 6), 300,
                                 shifts=(-1, 0, 1), sliding=True))
        self.assertLess(len(result.moves), len(moves))
        self.assertEqual(transform(result.moves), transform(moves))
        self.assertTrue(0 <= result.final_anchor <= len(result.moves))
        self.assertEqual(result.attempts[0].window_size, 6)

    def test_sliding_finds_detour_away_from_boundary(self):
        moves = parse_moves("R U D U' D' F2 L F R D B L U F R D")
        result = repair_solution(moves, len(moves), RepairConfig((6,), 100, sliding=True))
        self.assertTrue(any(a.kind == "sliding" and a.status == "FOUND" for a in result.attempts))
        self.assertLess(len(result.moves), len(moves))
        self.assertEqual(transform(result.moves), transform(moves))

    def test_invalid_config_or_solution(self):
        for kwargs in ({"windows": ()}, {"windows": (1,)}, {"time_limit_ms": -1},
                       {"time_limit_ms": float("inf")}, {"per_window_ms": 0}, {"max_states": 1}):
            with self.assertRaises(ValueError):
                RepairConfig(**kwargs)
        with self.assertRaises(ValueError):
            repair_solution(parse_moves("R U F D L B"), 3, initial_state=IDENTITY)


class BackendTests(unittest.TestCase):
    def test_actual_phase_cut_and_end_to_end(self):
        try:
            import kociemba
        except ImportError:
            self.skipTest("kociemba not installed")
        rng = random.Random(23)
        for _ in range(15):
            state = transform(tuple(rng.randrange(18) for _ in range(20)))
            base = initial_solution(state)
            self.assertTrue(in_g1(apply_moves(state, base.moves[:base.boundary])))
            self.assertTrue(all(m % 3 == 1 or FACES[m // 3] in "UD"
                                for m in base.moves[base.boundary:]))
            result = repair_solution(base.moves, base.boundary, RepairConfig((6, 8), 100), state)
            self.assertLessEqual(len(result.moves), len(base.moves))
            self.assertEqual(apply_moves(state, result.moves), IDENTITY)
        self.assertEqual(initial_solution(IDENTITY).moves, ())

    def test_inverse_phase_cut(self):
        self.assertEqual(reference_phase_cut(9, 10, False), (9, "suffix"))
        self.assertEqual(reference_phase_cut(9, 10, True), (10, "prefix"))

    def test_reference_does_not_auto_generate_tables(self):
        with self.assertRaisesRegex(RuntimeError, "init-twophase"):
            prepare_reference(ROOT / "tests" / "absent-reference-tables")
        self.assertFalse((ROOT / "tests" / "absent-reference-tables").exists())


class CliTests(unittest.TestCase):
    def _solve_json(self, *options):
        process = subprocess.run([sys.executable, str(ROOT / "solve.py"), "solve",
                                  "--time-ms", "0", "--json", *options],
                                 capture_output=True, text=True, timeout=10)
        self.assertEqual(process.returncode, 0, process.stderr)
        return json.loads(process.stdout)

    def test_auto_scramble_default_and_custom_length(self):
        try:
            import kociemba
        except ImportError:
            self.skipTest("kociemba not installed")
        for options, length in (((), 20), (("--scramble-length", "12"), 12)):
            report = self._solve_json("--seed", "123", *options)
            self.assertEqual(report["input_type"], "random_scramble")
            self.assertEqual(report["seed"], 123)
            self.assertEqual(report["scramble_length"], length)
            scramble = parse_moves(report["scramble"])
            self.assertEqual(len(scramble), length)
            self.assertTrue(all(a // 3 != b // 3 for a, b in zip(scramble, scramble[1:])))
            self.assertEqual(apply_moves(transform(scramble), parse_moves(report["solution"])), IDENTITY)
            self.assertTrue(report["verified"])

    def test_seed_reproduces_scramble_across_window_settings(self):
        try:
            import kociemba
        except ImportError:
            self.skipTest("kociemba not installed")
        first = self._solve_json("--seed", "456", "--window", "6")
        second = self._solve_json("--random", "--seed", "456", "--window", "8")
        self.assertEqual(first["scramble"], second["scramble"])

    def test_invalid_random_options(self):
        for options in (("--scramble-length", "0"), ("--scramble-length", "101"),
                        ("--scramble", "R", "--seed", "123"),
                        ("--facelets", SOLVED_FACELETS, "--scramble-length", "20"),
                        ("--random", "--scramble", "R")):
            process = subprocess.run([sys.executable, str(ROOT / "solve.py"), "solve", *options],
                                     capture_output=True, text=True, timeout=10)
            self.assertEqual(process.returncode, 2)

    def test_repair_json(self):
        process = subprocess.run([sys.executable, str(ROOT / "solve.py"), "repair",
                                  "--solution", "R U D | U' D' F2", "--window", "6",
                                  "--time-ms", "100", "--json"],
                                 capture_output=True, text=True, timeout=10, cwd="/tmp")
        self.assertEqual(process.returncode, 0, process.stderr)
        report = json.loads(process.stdout)
        self.assertEqual(report["final_length"], 2)
        self.assertTrue(report["verified"])
        self.assertFalse(report["global_optimality_proven"])

    def test_boundary_conflict(self):
        process = subprocess.run([sys.executable, str(ROOT / "solve.py"), "repair",
                                  "--solution", "R U D | U' D' F2", "--boundary", "2"],
                                 capture_output=True, text=True, timeout=10)
        self.assertEqual(process.returncode, 2)
        self.assertIn("separator", process.stderr)


if __name__ == "__main__":
    unittest.main()
