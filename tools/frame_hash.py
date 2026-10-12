"""Record and verify presented-frame references for relinked titles.

`APS5_DUMP_FRAMES=<n>` makes libSceAgcDriver write the first n presented display buffers as
`frame_<index>.bmp` in the working directory. Nothing compares those frames against anything, so a
change that alters rendering is only caught by a human looking at a screenshot.

This records a manifest of frame hashes for a run and verifies a later run against it. The manifest
holds hashes and per-channel means rather than images, because the repository does not carry image
files, and is line-oriented text rather than JSON, which the repository does not track either.

The hash is FNV-1a 64 over the pixel bytes in top-down order, matching the Hash() used by
ComputePipelineCapture so a C++ side can reproduce it.

    python tools/frame_hash.py record  <run-dir> --manifest frames.txt --device "RTX 4070 SUPER"
    python tools/frame_hash.py verify  <run-dir> --manifest frames.txt
    python tools/frame_hash.py selftest
"""

import argparse
import pathlib
import re
import struct
import sys

HEADER_BYTES = 54
MAX_PIXELS = 7680 * 4320
MAX_FRAMES = 4096
FRAME_NAME = re.compile(r"^frame_(\d+)\.bmp$")


class FrameError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise FrameError(message)


def fnv1a64(data):
    digest = 14695981039346656037
    for value in data:
        digest = ((digest ^ value) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{digest:016x}"


def read_bmp(path):
    raw = path.read_bytes()
    require(len(raw) >= HEADER_BYTES, f"{path.name}: shorter than a BMP header")
    magic, file_bytes, _, offset, info_bytes, width, height, planes_bpp, compression = struct.unpack_from("<HIIIIiiII", raw, 0)
    require(magic == 0x4D42, f"{path.name}: not a BMP")
    require(info_bytes == 40, f"{path.name}: unsupported info header of {info_bytes} bytes")
    require(offset == HEADER_BYTES, f"{path.name}: pixel data at {offset}, expected {HEADER_BYTES}")
    require(compression == 0, f"{path.name}: compressed BMPs are not supported")
    require(planes_bpp == (1 | (32 << 16)), f"{path.name}: only 32-bit single-plane frames are supported")
    require(width > 0 and height != 0, f"{path.name}: degenerate {width}x{height}")
    rows = abs(height)
    require(width * rows <= MAX_PIXELS, f"{path.name}: {width}x{rows} exceeds the 7680x4320 bound")
    stride = width * 4
    expected = stride * rows
    require(len(raw) - HEADER_BYTES >= expected, f"{path.name}: pixel data truncated")
    require(file_bytes == HEADER_BYTES + expected, f"{path.name}: file size field disagrees with the dimensions")
    pixels = raw[HEADER_BYTES:HEADER_BYTES + expected]
    if height > 0:
        pixels = b"".join(pixels[(rows - 1 - y) * stride:(rows - y) * stride] for y in range(rows))
    return width, rows, pixels


def channel_means(pixels):
    totals = [0, 0, 0, 0]
    for channel in range(4):
        totals[channel] = sum(pixels[channel::4])
    count = len(pixels) // 4
    return [round(total / count, 4) for total in totals]


def describe(path):
    width, height, pixels = read_bmp(path)
    return {
        "name": path.name,
        "width": width,
        "height": height,
        "hash": fnv1a64(pixels),
        "means": channel_means(pixels),
    }


def collect(directory, skip=0):
    directory = pathlib.Path(directory)
    require(directory.is_dir(), f"{directory} is not a directory")
    found = []
    for path in directory.iterdir():
        match = FRAME_NAME.match(path.name)
        if match and path.is_file():
            found.append((int(match.group(1)), path))
    require(found, f"{directory} holds no frame_<index>.bmp files; was APS5_DUMP_FRAMES set?")
    require(len(found) <= MAX_FRAMES, f"{directory} holds {len(found)} frames, more than {MAX_FRAMES}")
    found.sort()
    require(skip < len(found), f"{directory} holds {len(found)} frames, fewer than the {skip} skipped")
    return [describe(path) for _, path in found[skip:]]


def deltas(directory, frame):
    width, height, pixels = read_bmp(pathlib.Path(directory) / frame["name"])
    if width != frame["width"] or height != frame["height"]:
        return None
    means = channel_means(pixels)
    return max(abs(now - then) for now, then in zip(means, frame["means"]))


MANIFEST_BANNER = "# anyps5 frame reference 1"


def write_manifest(path, device, skip, frames):
    lines = [MANIFEST_BANNER, f"device {device}", f"skip {skip}"]
    for frame in frames:
        means = " ".join(f"{value:.4f}" for value in frame["means"])
        lines.append(f"frame {frame['name']} {frame['width']} {frame['height']} {frame['hash']} {means}")
    pathlib.Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def read_manifest(path):
    lines = pathlib.Path(path).read_text(encoding="utf-8").splitlines()
    require(lines and lines[0].strip() == MANIFEST_BANNER, f"{path} is not a frame reference manifest")
    manifest = {"device": "", "skip": 0, "frames": []}
    for line in lines[1:]:
        fields = line.split()
        if not fields or fields[0].startswith("#"):
            continue
        if fields[0] == "device":
            manifest["device"] = line.split(" ", 1)[1].strip() if " " in line else ""
        elif fields[0] == "skip":
            require(len(fields) == 2 and fields[1].isdigit(), f"{path}: malformed skip line")
            manifest["skip"] = int(fields[1])
        elif fields[0] == "frame":
            require(len(fields) == 9, f"{path}: a frame line needs a name, size, hash and four means")
            manifest["frames"].append({
                "name": fields[1],
                "width": int(fields[2]),
                "height": int(fields[3]),
                "hash": fields[4],
                "means": [float(value) for value in fields[5:]],
            })
        else:
            raise FrameError(f"{path}: unknown line {fields[0]!r}")
    require(manifest["frames"], f"{path} lists no frames")
    return manifest


def record(args):
    frames = collect(args.directory, args.skip)
    write_manifest(args.manifest, args.device, args.skip, frames)
    print(f"recorded {len(frames)} frames from {args.directory} for device {args.device!r}, skipping {args.skip}")
    return 0


def verify(args):
    manifest = read_manifest(args.manifest)
    expected = manifest["frames"]
    actual = collect(args.directory, manifest["skip"])
    if args.device and manifest["device"] and args.device != manifest["device"]:
        print(f"warning: manifest recorded on {manifest['device']!r}, verifying on {args.device!r}; GPU and driver change rendering", file=sys.stderr)
    if len(actual) != len(expected):
        print(f"frame count changed: manifest has {len(expected)}, run produced {len(actual)}", file=sys.stderr)
        return 1
    failures = 0
    for want, got in zip(expected, actual):
        if want["name"] != got["name"]:
            print(f"frame order changed: expected {want['name']}, found {got['name']}", file=sys.stderr)
            failures += 1
            continue
        if want["hash"] == got["hash"]:
            continue
        if want["width"] != got["width"] or want["height"] != got["height"]:
            print(f"{want['name']}: {want['width']}x{want['height']} became {got['width']}x{got['height']}", file=sys.stderr)
            failures += 1
            continue
        delta = deltas(args.directory, want)
        if args.tolerance <= 0:
            print(f"{want['name']}: hash {got['hash']} differs from the recorded {want['hash']}, mean channel delta {delta:.4f}", file=sys.stderr)
            failures += 1
            continue
        if delta is not None and delta <= args.tolerance:
            print(f"{want['name']}: pixels differ, mean channel delta {delta:.4f} within tolerance {args.tolerance}")
            continue
        print(f"{want['name']}: pixels differ, mean channel delta {delta:.4f} exceeds tolerance {args.tolerance}", file=sys.stderr)
        failures += 1
    if failures:
        print(f"{failures} of {len(expected)} frames regressed", file=sys.stderr)
        return 1
    print(f"{len(expected)} frames match the manifest")
    return 0


def write_bmp(path, width, height, pixels):
    header = struct.pack("<HIIIIiiIIIiiII", 0x4D42, HEADER_BYTES + len(pixels), 0, HEADER_BYTES, 40, width, -height, 1 | (32 << 16), 0, len(pixels), 2835, 2835, 0, 0)
    path.write_bytes(header + pixels)


def selftest(args):
    import tempfile
    with tempfile.TemporaryDirectory() as root:
        run = pathlib.Path(root) / "run"
        run.mkdir()
        base = bytes([(x * 7 + y * 13) % 256 for y in range(4) for x in range(4 * 4)])
        write_bmp(run / "frame_000.bmp", 4, 4, base)
        write_bmp(run / "frame_001.bmp", 4, 4, bytes(64))
        manifest = pathlib.Path(root) / "frames.txt"
        code = record(argparse.Namespace(directory=str(run), manifest=str(manifest), device="selftest", skip=0))
        require(code == 0, "record failed")
        recorded = read_manifest(manifest)
        require(len(recorded["frames"]) == 2, "expected two frames")
        require(recorded["frames"][0]["hash"] != recorded["frames"][1]["hash"], "distinct frames hashed the same")
        require(recorded["device"] == "selftest", "device did not round-trip")

        common = dict(manifest=str(manifest), device="selftest", tolerance=0.0)
        require(verify(argparse.Namespace(directory=str(run), **common)) == 0, "identical run did not verify")

        swapped = read_manifest(manifest)
        write_manifest(manifest, swapped["device"], swapped["skip"], [
            dict(swapped["frames"][0], hash="deadbeefdeadbeef"), swapped["frames"][1]])
        require(verify(argparse.Namespace(directory=str(run), **common)) == 1, "a hash that differs with unchanged means was not caught at tolerance 0")
        write_manifest(manifest, swapped["device"], swapped["skip"], swapped["frames"])
        require(verify(argparse.Namespace(directory=str(run), **common)) == 0, "restoring the manifest did not verify")

        nudged = bytearray(base)
        nudged[0] = (nudged[0] + 1) % 256
        write_bmp(run / "frame_000.bmp", 4, 4, bytes(nudged))
        require(verify(argparse.Namespace(directory=str(run), **common)) == 1, "a changed pixel was not caught")
        loose = dict(common, tolerance=1.0)
        require(verify(argparse.Namespace(directory=str(run), **loose)) == 0, "a one-step change was not tolerated")

        write_bmp(run / "frame_000.bmp", 4, 4, bytes(64))
        require(verify(argparse.Namespace(directory=str(run), **loose)) == 1, "a blanked frame was tolerated")

        write_bmp(run / "frame_000.bmp", 4, 4, bytes(nudged))
        (run / "frame_002.bmp").write_bytes((run / "frame_001.bmp").read_bytes())
        require(verify(argparse.Namespace(directory=str(run), **loose)) == 1, "an extra frame was not caught")
        (run / "frame_002.bmp").unlink()

        write_bmp(run / "frame_000.bmp", 8, 4, bytes(128))
        require(verify(argparse.Namespace(directory=str(run), **loose)) == 1, "a resolution change was tolerated")

        truncated = run / "frame_001.bmp"
        truncated.write_bytes(truncated.read_bytes()[:-4])
        try:
            collect(str(run))
        except FrameError:
            pass
        else:
            raise FrameError("a truncated frame was accepted")

        flipped = pathlib.Path(root) / "flipped"
        flipped.mkdir()
        rows = [base[y * 16:(y + 1) * 16] for y in range(4)]
        header = struct.pack("<HIIIIiiIIIiiII", 0x4D42, HEADER_BYTES + 64, 0, HEADER_BYTES, 40, 4, 4, 1 | (32 << 16), 0, 64, 2835, 2835, 0, 0)
        (flipped / "frame_000.bmp").write_bytes(header + b"".join(reversed(rows)))
        require(describe(flipped / "frame_000.bmp")["hash"] == recorded["frames"][0]["hash"], "bottom-up frame hashed differently from the same top-down image")
    print("frame_hash selftest passed")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Record and verify presented-frame references")
    commands = parser.add_subparsers(dest="command", required=True)

    recorder = commands.add_parser("record", help="write a manifest for a run directory")
    recorder.add_argument("directory")
    recorder.add_argument("--manifest", required=True)
    recorder.add_argument("--device", default="")
    recorder.add_argument("--skip", type=int, default=0, help="frames to ignore before the first recorded one; the first presented frame is not reproducible")
    recorder.set_defaults(handler=record)

    verifier = commands.add_parser("verify", help="compare a run directory against a manifest")
    verifier.add_argument("directory")
    verifier.add_argument("--manifest", required=True)
    verifier.add_argument("--device", default="")
    verifier.add_argument("--tolerance", type=float, default=0.0, help="largest tolerated per-channel mean difference")
    verifier.set_defaults(handler=verify)

    tester = commands.add_parser("selftest", help="check the reader, the hash and the comparison")
    tester.set_defaults(handler=selftest)

    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except FrameError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
