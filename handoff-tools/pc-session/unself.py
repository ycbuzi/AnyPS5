"""Reconstruct a clean ELF from a fake-signed PS5 SELF.

Segment data in a fake-signed SELF is plain (not encrypted, not compressed); it
just sits at container offsets instead of the offsets the program headers declare.
"""
import struct
import sys

SELF_MAGIC = b"\x4f\x15\x3d\x1d"


def main(inp, outp):
    data = open(inp, "rb").read()
    if data[:4] != SELF_MAGIC:
        raise SystemExit("not a SELF container")

    num_entries = struct.unpack_from("<H", data, 0x18)[0]
    elf_base = 0x20 + num_entries * 32
    if data[elf_base:elf_base + 4] != b"\x7fELF":
        raise SystemExit(f"no ELF at 0x{elf_base:x}")

    phoff = struct.unpack_from("<Q", data, elf_base + 32)[0]
    phentsize = struct.unpack_from("<H", data, elf_base + 54)[0]
    phnum = struct.unpack_from("<H", data, elf_base + 56)[0]

    phdrs = []
    for i in range(phnum):
        p = elf_base + phoff + i * phentsize
        ptype = struct.unpack_from("<I", data, p)[0]
        off = struct.unpack_from("<Q", data, p + 8)[0]
        filesz = struct.unpack_from("<Q", data, p + 32)[0]
        phdrs.append({"i": i, "type": ptype, "off": off, "filesz": filesz})

    out_size = max(h["off"] + h["filesz"] for h in phdrs)
    print(f"output ELF size: 0x{out_size:x} ({out_size:,} bytes)")
    out = bytearray(out_size)

    hdr_bytes = phoff + phnum * phentsize
    out[0:hdr_bytes] = data[elf_base:elf_base + hdr_bytes]
    print(f"copied ELF header + {phnum} program headers (0x{hdr_bytes:x} bytes)")

    placed = set()
    last_end = 0
    for i in range(num_entries):
        p = 0x20 + i * 32
        props, off, filesz, _memsz = struct.unpack_from("<QQQQ", data, p)
        last_end = max(last_end, off + filesz)
        if not props & 0x800:            # only blocked entries carry segment data
            continue
        seg = (props >> 20) & 0xFFF
        ph = next((h for h in phdrs if h["i"] == seg), None)
        if ph is None or ph["filesz"] != filesz:
            print(f"  entry {i:2}: no matching phdr {seg}, skipped")
            continue
        out[ph["off"]:ph["off"] + filesz] = data[off:off + filesz]
        placed.add(seg)
        print(f"  entry {i:2} -> phdr {seg:2}  0x{filesz:08x} bytes  "
              f"container 0x{off:08x} -> elf 0x{ph['off']:08x}")

    tail = len(data) - last_end
    if tail > 0:
        cands = [h for h in phdrs if h["i"] not in placed and h["filesz"] == tail]
        if len(cands) == 1:
            h = cands[0]
            out[h["off"]:h["off"] + tail] = data[last_end:last_end + tail]
            placed.add(h["i"])
            print(f"  tail 0x{tail:x} bytes -> phdr {h['i']} at elf 0x{h['off']:08x}")
        else:
            print(f"  tail 0x{tail:x} bytes: {len(cands)} candidates, left unplaced")

    for h in phdrs:
        if h["filesz"] == 0 or h["i"] in placed:
            continue
        covered = any(
            o["i"] in placed and h["off"] >= o["off"]
            and h["off"] + h["filesz"] <= o["off"] + o["filesz"]
            for o in phdrs
        )
        if not covered:
            print(f"  WARNING phdr {h['i']} type=0x{h['type']:x} "
                  f"off=0x{h['off']:x} size=0x{h['filesz']:x} has no source")

    open(outp, "wb").write(bytes(out))
    print(f"wrote {outp} ({out_size:,} bytes)")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
