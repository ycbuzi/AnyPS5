import argparse
import os
from pathlib import Path
import tempfile
import time


BUTTONS = {
    "l3": 0x2, "r3": 0x4, "options": 0x8,
    "up": 0x10, "right": 0x20, "down": 0x40, "left": 0x80,
    "l2": 0x100, "r2": 0x200, "l1": 0x400, "r1": 0x800,
    "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000,
    "square": 0x8000, "touchpad": 0x100000, "release": 0,
}


def main():
    parser = argparse.ArgumentParser(description="Queue a controller pulse for APS5_PAD_INPUT_FILE.")
    parser.add_argument("path", type=Path)
    parser.add_argument("button", choices=BUTTONS)
    parser.add_argument("--hold-ms", type=int, default=300)
    parser.add_argument("--left-stick", type=int, nargs=2, metavar=("X", "Y"))
    parser.add_argument("--right-stick", type=int, nargs=2, metavar=("X", "Y"))
    args = parser.parse_args()
    if not 1 <= args.hold_ms <= 5000:
        parser.error("--hold-ms must be between 1 and 5000")
    sticks = ""
    if args.left_stick is not None or args.right_stick is not None:
        axes = (args.left_stick or [128, 128]) + (args.right_stick or [128, 128])
        if not all(0 <= axis <= 255 for axis in axes):
            parser.error("stick axes must be between 0 and 255")
        sticks = "".join(f" {axis}" for axis in axes)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", dir=args.path.parent, delete=False) as output:
            temporary = Path(output.name)
            output.write(f"{time.monotonic_ns()} {BUTTONS[args.button]} {args.hold_ms}{sticks}\n")
        os.replace(temporary, args.path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(f"Queued {args.button}{' with sticks' + sticks if sticks else ''} for {args.hold_ms} ms in {args.path}")


if __name__ == "__main__":
    main()
