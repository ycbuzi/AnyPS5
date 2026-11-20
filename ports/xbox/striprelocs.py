import struct
import sys


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = bytearray(open(src, "rb").read())
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    chars = struct.unpack_from("<H", data, pe + 22)[0]
    struct.pack_into("<H", data, pe + 22, chars | 1)
    opt = pe + 24
    dllchars_at = opt + 70
    dllchars = struct.unpack_from("<H", data, dllchars_at)[0]
    struct.pack_into("<H", data, dllchars_at, dllchars & ~0x40)
    reloc = opt + 112 + 5 * 8
    print("reloc dir", struct.unpack_from("<II", data, reloc), "chars", hex(chars | 1), "dllchars", hex(dllchars & ~0x40))
    struct.pack_into("<II", data, reloc, 0, 0)
    open(dst, "wb").write(data)


if __name__ == "__main__":
    main()
