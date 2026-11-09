"""Exact cubie transformations in standard URFDLB / Kociemba coordinates.

A state has 8 corner bytes (3 * cubie + orientation) followed by 12 edge
bytes (2 * cubie + orientation). No lossy coordinates or hash-only equality.
"""
from collections import Counter
import re

FACES = "URFDLB"
IDENTITY = bytes([3 * i for i in range(8)] + [2 * i for i in range(12)])
SOLVED_FACELETS = "".join(face * 9 for face in FACES)

# Conventional cubie positions: URF UFL ULB UBR DFR DLF DBL DRB;
# UR UF UL UB DR DF DL DB FR FL BL BR.
_QUARTERS = (
    ((3, 0, 1, 2, 4, 5, 6, 7), (0,) * 8,
     (3, 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11), (0,) * 12),
    ((4, 1, 2, 0, 7, 5, 6, 3), (2, 0, 0, 1, 1, 0, 0, 2),
     (8, 1, 2, 3, 11, 5, 6, 7, 4, 9, 10, 0), (0,) * 12),
    ((1, 5, 2, 3, 0, 4, 6, 7), (1, 2, 0, 0, 2, 1, 0, 0),
     (0, 9, 2, 3, 4, 8, 6, 7, 1, 5, 10, 11),
     (0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0)),
    ((0, 1, 2, 3, 5, 6, 7, 4), (0,) * 8,
     (0, 1, 2, 3, 5, 6, 7, 4, 8, 9, 10, 11), (0,) * 12),
    ((0, 2, 6, 3, 4, 1, 5, 7), (0, 1, 2, 0, 0, 2, 1, 0),
     (0, 1, 10, 3, 4, 5, 9, 7, 8, 2, 6, 11), (0,) * 12),
    ((0, 1, 3, 7, 4, 5, 2, 6), (0, 0, 1, 2, 0, 0, 2, 1),
     (0, 1, 2, 11, 4, 5, 6, 10, 8, 9, 3, 7),
     (0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1)),
)
CORNER_FACELETS = ((8, 9, 20), (6, 18, 38), (0, 36, 47), (2, 45, 11),
                   (29, 26, 15), (27, 44, 24), (33, 53, 42), (35, 17, 51))
CORNER_COLORS = ("URF", "UFL", "ULB", "UBR", "DFR", "DLF", "DBL", "DRB")
EDGE_FACELETS = ((5, 10), (7, 19), (3, 37), (1, 46), (32, 16), (28, 25),
                 (30, 43), (34, 52), (23, 12), (21, 41), (50, 39), (48, 14))
EDGE_COLORS = ("UR", "UF", "UL", "UB", "DR", "DF", "DL", "DB", "FR", "FL", "BL", "BR")


def compose(a: bytes, b: bytes) -> bytes:
    """Return a * b: apply transformation b to state a."""
    corners = []
    for v in b[:8]:
        av = a[v // 3]
        corners.append(3 * (av // 3) + (av % 3 + v % 3) % 3)
    edges = []
    for v in b[8:]:
        av = a[8 + v // 2]
        edges.append((av & ~1) | ((av ^ v) & 1))
    return bytes(corners + edges)


def inverse(state: bytes) -> bytes:
    out = bytearray(20)
    for i, v in enumerate(state[:8]):
        out[v // 3] = 3 * i + (-v % 3)
    for i, v in enumerate(state[8:]):
        out[8 + v // 2] = 2 * i + v % 2
    return bytes(out)


def _move_maps():
    result = []
    for cp, co, ep, eo in _QUARTERS:
        quarter = bytes([3 * p + o for p, o in zip(cp, co)] +
                        [2 * p + o for p, o in zip(ep, eo)])
        state = IDENTITY
        for _ in range(3):
            state = compose(state, quarter)
            result.append(state)
    return tuple(result)


MOVE_MAPS = _move_maps()


def parse_moves(text: str) -> tuple[int, ...]:
    """Accept R U R' F2 and reference-backend notation R1 U3 (4f)."""
    text = re.sub(r"\(\d+f\)\s*$", "", text.strip())
    moves = []
    for token in text.split():
        if not re.fullmatch(r"[URFDLB](?:[123]|')?", token):
            raise ValueError(f"잘못된 move: {token!r}; 예: R U R' F2")
        power = {"": 1, "1": 1, "2": 2, "3": 3, "'": 3}[token[1:]]
        moves.append(3 * FACES.index(token[0]) + power - 1)
    return tuple(moves)


def format_moves(moves) -> str:
    return " ".join(FACES[m // 3] + ("", "2", "'")[m % 3] for m in moves)


def inverse_moves(moves) -> tuple[int, ...]:
    return tuple(3 * (m // 3) + 2 - m % 3 for m in reversed(moves))


def apply_moves(state: bytes, moves) -> bytes:
    for move in moves:
        state = compose(state, MOVE_MAPS[move])
    return state


def transform(moves) -> bytes:
    return apply_moves(IDENTITY, moves)


def normalize(moves) -> tuple[int, ...]:
    """Combine adjacent same-face turns; never increase HTM length."""
    stack = []
    for m in moves:
        if stack and stack[-1] // 3 == m // 3:
            previous = stack.pop()
            power = (previous % 3 + m % 3 + 2) % 4
            if power:
                stack.append(3 * (m // 3) + power - 1)
        else:
            stack.append(m)
    return tuple(stack)


def validate_state(state: bytes):
    if len(state) != 20:
        raise ValueError("cubie state는 20바이트여야 합니다")
    cp = [v // 3 for v in state[:8]]
    ep = [v // 2 for v in state[8:]]
    if sorted(cp) != list(range(8)) or sorted(ep) != list(range(12)):
        raise ValueError("중복되거나 잘못된 cubie가 있습니다")
    if sum(v % 3 for v in state[:8]) % 3 or sum(v % 2 for v in state[8:]) % 2:
        raise ValueError("corner twist / edge flip 합이 유효하지 않습니다")
    parity = lambda p: sum(p[i] > p[j] for i in range(len(p))
                           for j in range(i + 1, len(p))) % 2
    if parity(cp) != parity(ep):
        raise ValueError("corner / edge permutation parity가 다릅니다")


def from_facelets(text: str) -> bytes:
    text = text.strip().upper()
    if len(text) != 54 or Counter(text) != Counter(SOLVED_FACELETS):
        raise ValueError("facelets는 URFDLB 각 색 9개, 총 54개여야 합니다")
    if "".join(text[i * 9 + 4] for i in range(6)) != FACES:
        raise ValueError("중심 색과 면 순서는 URFDLB여야 합니다")
    out = []
    for positions, colors, size in ((CORNER_FACELETS, CORNER_COLORS, 3),
                                    (EDGE_FACELETS, EDGE_COLORS, 2)):
        for facelets in positions:
            observed = "".join(text[i] for i in facelets)
            matches = [size * cubie + ori for cubie, color in enumerate(colors)
                       for ori in range(size)
                       if all(observed[(k + ori) % size] == color[k] for k in range(size))]
            if len(matches) != 1:
                raise ValueError(f"유효하지 않은 cubie 색: {observed}")
            out.append(matches[0])
    state = bytes(out)
    validate_state(state)
    return state


def to_facelets(state: bytes) -> str:
    validate_state(state)
    out = list(SOLVED_FACELETS)
    for values, positions, colors, size in ((state[:8], CORNER_FACELETS, CORNER_COLORS, 3),
                                           (state[8:], EDGE_FACELETS, EDGE_COLORS, 2)):
        for v, facelets in zip(values, positions):
            for k, color in enumerate(colors[v // size]):
                out[facelets[(k + v % size) % size]] = color
    return "".join(out)


def in_g1(state: bytes) -> bool:
    """Standard UD-axis subgroup: twist=flip=0, E-slice edges in E-slice."""
    return (all(v % 3 == 0 for v in state[:8]) and
            all(v % 2 == 0 for v in state[8:]) and
            all(v // 2 >= 8 for v in state[16:]))
