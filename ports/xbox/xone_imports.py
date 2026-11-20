import collections
import json
import os
import sys

import pefile

DEVMODE_ABSENT = {"kernelx.dll", "d3d12_x.dll", "d3d11_x.dll", "xg_x.dll", "xgameruntime.dll", "xmem.dll", "xapu.dll",
                  "microsoft.xbox.services.dll", "dxcompiler_x.dll"}


def imports(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
                                           pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT"]])
    out = collections.OrderedDict()
    for attr in ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT"):
        for entry in getattr(pe, attr, []):
            dll = entry.dll.decode(errors="replace").lower()
            names = [(i.name.decode(errors="replace") if i.name else "#%d" % i.ordinal) for i in entry.imports]
            out.setdefault(dll, []).extend(names)
    info = {"machine": hex(pe.FILE_HEADER.Machine), "subsystem": pe.OPTIONAL_HEADER.Subsystem,
            "image_mb": round(pe.OPTIONAL_HEADER.SizeOfImage / 1048576, 1),
            "sections": [s.Name.rstrip(b"\0").decode(errors="replace") for s in pe.sections]}
    return info, out


def main():
    folder = sys.argv[1]
    report = {}
    for name in sorted(os.listdir(folder)):
        if not name.lower().endswith((".exe", ".dll")):
            continue
        info, imp = imports(os.path.join(folder, name))
        report[name] = {"info": info, "imports": imp}
        print("==", name, info)
        for dll, funcs in imp.items():
            tag = " [GAME OS ONLY]" if dll in DEVMODE_ABSENT else (" [api set]" if dll.startswith(("api-ms-", "ext-ms-")) else "")
            print("  %-48s %4d%s" % (dll, len(funcs), tag))
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)


if __name__ == "__main__":
    main()
