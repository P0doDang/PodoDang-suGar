"""Random-walk scramble generation shared by the CLI and benchmark."""
import random


def random_scramble(length: int = 20, *, rng: random.Random | None = None) -> tuple[int, ...]:
    """Generate HTM moves without consecutive turns on the same face.

    This samples move sequences, not uniformly random cube states. Passing an
    existing seeded RNG preserves its stream across repeated benchmark trials.
    """
    if isinstance(length, bool) or not isinstance(length, int) or not 1 <= length <= 100:
        raise ValueError("scramble-length는 1~100 정수여야 합니다")
    rng = rng if rng is not None else random.Random()
    moves = []
    for _ in range(length):
        choices = [m for m in range(18) if not moves or m // 3 != moves[-1] // 3]
        moves.append(rng.choice(choices))
    return tuple(moves)
