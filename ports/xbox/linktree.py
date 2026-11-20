import os
import shutil
import sys


def main():
    src, dst = sys.argv[1], sys.argv[2]
    skip = set(n.lower() for n in sys.argv[3:])
    linked = copied = 0
    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        out = os.path.join(dst, rel)
        os.makedirs(out, exist_ok=True)
        for name in files:
            if name.lower() in skip:
                continue
            target = os.path.join(out, name)
            if os.path.exists(target):
                continue
            try:
                os.link(os.path.join(root, name), target)
                linked += 1
            except OSError:
                shutil.copy2(os.path.join(root, name), target)
                copied += 1
    print("linked", linked, "copied", copied)


if __name__ == "__main__":
    main()
