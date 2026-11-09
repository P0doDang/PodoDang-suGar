"""Independent 54-sticker model using integer 3-D rotations, for verification."""
import random

COLORS = "URFDLB"
SOLVED = "".join(c * 9 for c in COLORS)
NORMALS = {"U": (0, 1, 0), "R": (1, 0, 0), "F": (0, 0, 1),
           "D": (0, -1, 0), "L": (-1, 0, 0), "B": (0, 0, -1)}


def _position(face, row, col):
    return {"U": (col - 1, 1, row - 1), "R": (1, 1 - row, 1 - col),
            "F": (col - 1, 1 - row, 1), "D": (col - 1, -1, 1 - row),
            "L": (-1, 1 - row, col - 1), "B": (1 - col, 1 - row, -1)}[face]


STICKERS = [(_position(f, r, c), NORMALS[f]) for f in COLORS
            for r in range(3) for c in range(3)]
INDEX = {item: i for i, item in enumerate(STICKERS)}


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _clockwise(v, n):
    cross = (n[1] * v[2] - n[2] * v[1], n[2] * v[0] - n[0] * v[2],
             n[0] * v[1] - n[1] * v[0])
    parallel = _dot(v, n)
    return tuple(parallel * x - y for x, y in zip(n, cross))


def _quarter(face):
    n = NORMALS[face]
    p = list(range(54))
    for source, (pos, normal) in enumerate(STICKERS):
        if _dot(pos, n) == 1:
            target = INDEX[(_clockwise(pos, n), _clockwise(normal, n))]
            p[target] = source
    return p


PERMUTATIONS = {}
for _face in COLORS:
    _q = _quarter(_face)
    _p = list(range(54))
    for _power, _suffix in enumerate(("", "2", "'"), 1):
        _p = [_p[i] for i in _q]
        PERMUTATIONS[_face + _suffix] = tuple(_p)


def apply(facelets, algorithm):
    for token in algorithm.split():
        facelets = "".join(facelets[i] for i in PERMUTATIONS[token])
    return facelets


def parity(p):
    return sum(p[i] > p[j] for i in range(len(p)) for j in range(i + 1, len(p))) % 2


def uniform_state(rng: random.Random):
    """Uniform legal cubie state, with no scramble-length distribution assumption."""
    cp, ep = list(range(8)), list(range(12))
    rng.shuffle(cp)
    rng.shuffle(ep)
    if parity(cp) != parity(ep):
        ep[0], ep[1] = ep[1], ep[0]
    co = [rng.randrange(3) for _ in range(7)]
    co.append(-sum(co) % 3)
    eo = [rng.randrange(2) for _ in range(11)]
    eo.append(sum(eo) % 2)
    cf = ((8, 9, 20), (6, 18, 38), (0, 36, 47), (2, 45, 11),
          (29, 26, 15), (27, 44, 24), (33, 53, 42), (35, 17, 51))
    ef = ((5, 10), (7, 19), (3, 37), (1, 46), (32, 16), (28, 25),
          (30, 43), (34, 52), (23, 12), (21, 41), (50, 39), (48, 14))
    cc = ("URF", "UFL", "ULB", "UBR", "DFR", "DLF", "DBL", "DRB")
    ec = ("UR", "UF", "UL", "UB", "DR", "DF", "DL", "DB", "FR", "FL", "BL", "BR")
    out = ["?"] * 54
    for i, face in enumerate(COLORS):
        out[i * 9 + 4] = face
    for i in range(8):
        for j in range(3):
            out[cf[i][(j + co[i]) % 3]] = cc[cp[i]][j]
    for i in range(12):
        for j in range(2):
            out[ef[i][(j + eo[i]) % 2]] = ec[ep[i]][j]
    return "".join(out)


def shallow_distances(radius=4):
    """Unpruned BFS in the independent sticker model supplies exact HTM answers."""
    distances = {SOLVED: 0}
    frontier = [SOLVED]
    for d in range(1, radius + 1):
        next_frontier = []
        for s in frontier:
            for p in PERMUTATIONS.values():
                t = "".join(s[i] for i in p)
                if t not in distances:
                    distances[t] = d
                    next_frontier.append(t)
        frontier = next_frontier
    return distances
