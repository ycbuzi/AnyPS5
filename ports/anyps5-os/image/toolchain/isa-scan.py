#!/usr/bin/env python3
"""Counts instructions a CPU without AVX, AVX2, FMA, F16C, BMI1, BMI2 and LZCNT cannot run, per file and per function.

usage: isa-scan.py [--functions N] FILE...
Output: one "FILE <path> vex=.. ymm=.. zmm=.. fma=.. f16c=.. bmi=.. lzcnt=.. tzcnt=.. total=.." line per file, then up to
N "  FUNC <count> <name> [kinds]" lines, functions whose names do not mark a CPU-specific variant first, and a
"TOTAL" line. outside_variants counts the instructions in functions whose names carry no variant marker (avx2, evex,
bmi2, fma, ...): those are the ones to check by hand for an unconditional path.
A count says the bytes are there, not that they run: code behind a CPUID check (glibc ifuncs, __builtin_cpu_supports,
FFmpeg's av_get_cpu_flags) holds such instructions too. objdump decodes data inside code sections as well, so a few
counts in a game binary can come from jump tables. A file whose sections hold no code objdump can show (a relinked
game) is disassembled per executable PT_LOAD segment instead, grouped by 64 KiB page, with the first N hit addresses as
"  ADDR" lines; that linear sweep also decodes data. Needs objdump (binutils).
"""
import re
import subprocess
import sys

NOT_VEX = {"verr", "verw", "vmcall", "vmlaunch", "vmresume", "vmxoff", "vmxon", "vmread", "vmwrite", "vmptrld",
           "vmptrst", "vmclear", "vmfunc", "vmrun", "vmload", "vmsave", "vmmcall", "vmgexit"}
PREFIXES = {"lock", "rep", "repz", "repnz", "repe", "repne", "data16", "addr32", "cs", "ds", "es", "fs", "gs", "ss",
            "notrack", "bnd", "xacquire", "xrelease", "rex", "rex.w", "rex.b", "rex.x", "rex.r", "{vex}", "{vex3}",
            "{evex}", "{disp32}"}
BMI = {"andn", "bextr", "blsi", "blsmsk", "blsr", "bzhi", "pdep", "pext", "rorx", "sarx", "shlx", "shrx", "mulx"}
ELF_MAGIC = bytes([0x7F]) + b"ELF"
KINDS = ("vex", "ymm", "zmm", "fma", "f16c", "bmi", "lzcnt", "tzcnt")
LINE = re.compile(r"^\s*[0-9a-f]+:\s+(.*)$")
FUNC = re.compile(r"^[0-9a-f]+ <(.+)>:$")
VARIANT = re.compile(r"avx|evex|bmi|fma|f16c|x86_64_v[34]|haswell|skylake|znver|_rtm|lzcnt|tzcnt", re.IGNORECASE)


def classify(text):
    parts = text.split()
    while parts and (parts[0].lower() in PREFIXES or parts[0].lower().startswith("rex")):
        parts = parts[1:]
    if not parts:
        return ()
    mnemonic = parts[0].lower()
    base = re.sub(r"[bwlq]$", "", mnemonic)
    kinds = []
    if mnemonic.startswith("v") and mnemonic not in NOT_VEX and not mnemonic.startswith("vm"):
        kinds.append("vex")
    elif mnemonic.startswith("vm") and mnemonic not in NOT_VEX and len(mnemonic) > 3 and mnemonic[2:4] in ("ov", "ax", "in", "ul", "as", "ps", "pd"):
        kinds.append("vex")
    if "%ymm" in text:
        kinds.append("ymm")
    if "%zmm" in text or re.search(r"%k[0-7]\b", text):
        kinds.append("zmm")
    if re.match(r"vf(n)?m(add|sub|addsub|subadd)", mnemonic):
        kinds.append("fma")
    if mnemonic in ("vcvtph2ps", "vcvtps2ph"):
        kinds.append("f16c")
    if mnemonic in BMI or base in BMI:
        kinds.append("bmi")
    if mnemonic in ("lzcnt",) or base == "lzcnt":
        kinds.append("lzcnt")
    if mnemonic in ("tzcnt",) or base == "tzcnt":
        kinds.append("tzcnt")
    return kinds


def executable_segments(path):
    import struct
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != ELF_MAGIC or data[4] != 2:
        return []
    phoff, = struct.unpack_from("<Q", data, 32)
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    segments = []
    for i in range(phnum):
        p_type, p_flags, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<IIQQQQ", data, phoff + i * phentsize)
        if p_type == 1 and p_flags & 1 and p_filesz:
            segments.append((p_vaddr, data[p_offset:p_offset + p_filesz]))
    return segments


def sections_cover_code(path):
    import struct
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != ELF_MAGIC or data[4] != 2:
        return True
    shoff, = struct.unpack_from("<Q", data, 40)
    shentsize, shnum = struct.unpack_from("<HH", data, 58)
    in_sections = 0
    for i in range(shnum if shoff else 0):
        sh_type, = struct.unpack_from("<I", data, shoff + i * shentsize + 4)
        flags, = struct.unpack_from("<Q", data, shoff + i * shentsize + 8)
        size, = struct.unpack_from("<Q", data, shoff + i * shentsize + 32)
        if sh_type == 1 and flags & 4:
            in_sections += size
    in_segments = sum(len(blob) for _, blob in executable_segments(path))
    return in_sections * 2 >= in_segments


def segment_listings(path):
    import tempfile
    for vaddr, blob in executable_segments(path):
        with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
            tmp.write(blob)
            tmp.flush()
            yield vaddr, ["objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "--no-show-raw-insn", "-M", "suffix",
                          f"--adjust-vma=0x{vaddr:x}", tmp.name]


def scan(path, top):
    counts = dict.fromkeys(KINDS, 0)
    total = 0
    functions = {}
    addresses = []
    stripped = False
    seen = 0

    def passes():
        if sections_cover_code(path):
            yield None, ["objdump", "-d", "--no-show-raw-insn", "-M", "suffix", path]
        if seen == 0:
            yield from segment_listings(path)

    for vaddr, cmd in passes():
        stripped = stripped or vaddr is not None
        current = "?"
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors="replace")
        for line in p.stdout:
            line = line.rstrip("\n")
            m = FUNC.match(line)
            if m and vaddr is None:
                current = m.group(1)
                continue
            m = LINE.match(line)
            if not m:
                continue
            seen += 1
            kinds = classify(m.group(1))
            if not kinds:
                continue
            total += 1
            for k in kinds:
                counts[k] += 1
            name = current
            if vaddr is not None:
                address = int(line.split(":", 1)[0], 16)
                name = f"segment 0x{vaddr:x} page 0x{address & ~0xFFFF:x}"
                if len(addresses) < top:
                    addresses.append(f"  ADDR 0x{address:x} {m.group(1).strip()} [{','.join(kinds)}]")
            entry = functions.setdefault(name, [0, set()])
            entry[0] += 1
            entry[1].update(kinds)
        p.wait()
    unnamed = sum(n for name, (n, _) in functions.items() if not VARIANT.search(name))
    note = " no_symbols=segments" if stripped else ""
    out = [f"FILE {path} " + " ".join(f"{k}={counts[k]}" for k in KINDS) + f" total={total} outside_variants={unnamed}{note}"]
    ranked = sorted(functions.items(), key=lambda item: (bool(VARIANT.search(item[0])), -item[1][0]))
    for name, (n, kinds) in ranked[:top]:
        out.append(f"  FUNC {n} {name} [{','.join(sorted(kinds))}]" + (" variant" if VARIANT.search(name) else ""))
    out += addresses
    return counts, total, out, unnamed


def main(argv):
    top = 12
    files = []
    i = 0
    while i < len(argv):
        if argv[i] == "--functions" and i + 1 < len(argv):
            top = int(argv[i + 1])
            i += 2
            continue
        files.append(argv[i])
        i += 1
    if not files:
        print(__doc__)
        return 1
    grand = dict.fromkeys(KINDS, 0)
    grand_total = 0
    grand_unnamed = 0
    for path in files:
        counts, total, out, unnamed = scan(path, top)
        grand_unnamed += unnamed
        print("\n".join(out), flush=True)
        for k in KINDS:
            grand[k] += counts[k]
        grand_total += total
    print("TOTAL " + " ".join(f"{k}={grand[k]}" for k in KINDS) + f" total={grand_total} outside_variants={grand_unnamed}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
