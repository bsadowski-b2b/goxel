#!/usr/bin/env python3
"""Generate deterministic TXT voxel fixtures for GPU-upgrade baselines."""

from pathlib import Path
import argparse


SCENES = {
    "small": (24, 3),
    "medium": (64, 5),
    "stress": (128, 7),
}


def occupied(x: int, y: int, z: int, size: int, stride: int) -> bool:
    shell = x in (0, size - 1) or y in (0, size - 1) or z in (0, size - 1)
    lattice = (x % stride == 0 and y % stride == 0) or (
        y % stride == 0 and z % stride == 0
    ) or (z % stride == 0 and x % stride == 0)
    sphere = (
        (x - size // 2) ** 2
        + (y - size // 2) ** 2
        + (z - size // 2) ** 2
        < (size // 4) ** 2
    )
    return shell or lattice or sphere


def generate(path: Path, size: int, stride: int) -> int:
    count = 0
    offset = size // 2
    with path.open("w", encoding="ascii") as output:
        for z in range(size):
            for y in range(size):
                for x in range(size):
                    if not occupied(x, y, z, size, stride):
                        continue
                    color = (
                        ((x * 17 + z * 3) & 0xFF) << 16
                        | ((y * 13 + x * 5) & 0xFF) << 8
                        | ((z * 11 + y * 7) & 0xFF)
                    )
                    output.write(
                        f"{x - offset} {y - offset} {z - offset} {color:06X}\n"
                    )
                    count += 1
    return count


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("benchmarks/gpu"),
        help="fixture output directory",
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name, (size, stride) in SCENES.items():
        path = args.output / f"gpu-{name}.txt"
        count = generate(path, size, stride)
        print(f"{path}: {count} voxels")


if __name__ == "__main__":
    main()
