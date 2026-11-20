import bisect
import struct
import sys

import pefile


def symbols(path):
    pe = pefile.PE(path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    table = pe.FILE_HEADER.PointerToSymbolTable
    count = pe.FILE_HEADER.NumberOfSymbols
    data = pe.__data__
    out = []
    if table and count:
        strings = table + count * 18
        i = 0
        while i < count:
            raw = data[table + i * 18: table + i * 18 + 18]
            name, value, section, kind, storage, aux = struct.unpack("<8sIhHBB", raw)
            if name[:4] == b"\0\0\0\0":
                offset = struct.unpack("<I", name[4:])[0]
                end = data.find(b"\0", strings + offset)
                text = data[strings + offset:end].decode(errors="replace")
            else:
                text = name.rstrip(b"\0").decode(errors="replace")
            if section > 0 and (kind & 0x20 or storage in (2, 3)):
                out.append((pe.sections[section - 1].VirtualAddress + value, text))
            i += 1 + aux
    for e in getattr(pe, "DIRECTORY_ENTRY_EXPORT", None).symbols if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") else []:
        if e.name:
            out.append((e.address, e.name.decode(errors="replace")))
    out.sort()
    return out, count


def main():
    syms, count = symbols(sys.argv[1])
    print("coff symbols:", count, "usable:", len(syms))
    addrs = [s[0] for s in syms]
    for arg in sys.argv[2:]:
        rva = int(arg, 16)
        i = bisect.bisect_right(addrs, rva) - 1
        print(arg, syms[i][1] + "+0x%x" % (rva - syms[i][0]) if i >= 0 else "?")


if __name__ == "__main__":
    main()
