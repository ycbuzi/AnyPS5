import os, sys

OLD = b"ImmGetIMEFileNameA\x00"
NEW = b"ImmGetContext".ljust(len(OLD), b"\x00")


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for path in sys.argv[2:]:
        data = open(path, "rb").read()
        count = data.count(OLD)
        if count:
            open(os.path.join(out, os.path.basename(path)), "wb").write(data.replace(OLD, NEW))
        print(os.path.basename(path), "patched", count)


if __name__ == "__main__":
    main()
