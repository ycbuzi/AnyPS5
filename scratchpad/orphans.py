"""Find .cpp files that exist on disk but are in no CMake target.

Every sync regression this session had one shape: taking upstream's version of a
CMakeLists silently drops a harvested source file, the library still builds, and
the missing symbol only surfaces when some unrelated test target links against it
build-cycles later (UserData.cpp -> libSceAgcDriver, UserContext.cpp ->
libSceLibcInternal). An orphaned source file is mechanically detectable, so detect
it instead of paying a 20-minute build to rediscover it.

Two things this has to get right, both learned the hard way:
  * follow include(...) -- libSceFont lists its sources in FontLibrary.cmake, so a
    scanner that only reads CMakeLists.txt calls every one of them orphaned.
  * match on path, not basename -- libkernel has its own System/src/UserContext.cpp,
    so a basename-global match would have missed the very bug this exists to catch.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "D:/AnyPS5")
PRX = ROOT / "core" / "libs" / "prx"
CPP = re.compile(r"[\w${}/\\.-]+\.cpp")
INCLUDE = re.compile(r"include\s*\(\s*([^)\s]+)", re.I)
VAR = re.compile(r"\$\{(?:CMAKE_CURRENT_SOURCE_DIR|CMAKE_CURRENT_LIST_DIR)\}[/\\]?")


def cmake_text(path, base, seen):
    """Text of a cmake file plus everything it include()s, resolved against base."""
    if path in seen or not path.is_file():
        return ""
    seen.add(path)
    text = path.read_text(encoding="utf-8", errors="ignore")
    out = [text]
    for raw in INCLUDE.findall(text):
        rel = VAR.sub("", raw).strip('"')
        if rel.endswith(".cmake"):
            out.append(cmake_text(base / rel, base, seen))
    return "\n".join(out)


def claimed_paths(cml):
    base = cml.parent
    text = cmake_text(cml, base, set())
    out = set()
    for tok in CPP.findall(text):
        rel = VAR.sub("", tok).replace("\\", "/").strip('"')
        out.add(rel)
        out.add(rel.rsplit("/", 1)[-1])  # bare name, for same-dir listings
    return out


# libc unity-builds its exception support: src/exception/Exports.cpp does
# #include "Unwind.cpp" for eight siblings, and Unwind.cpp in turn includes
# specifics/x86_64/RegisterContext.cpp. Those are compiled, just not by CMake.
INCLUDED_CPP = set()
for src in (ROOT / "core" / "libs").rglob("*.cpp"):
    for hit in re.findall(r'#include\s+"([^"]+\.cpp)"', src.read_text(encoding="utf-8", errors="ignore")):
        INCLUDED_CPP.add(hit.replace("\\", "/").rsplit("/", 1)[-1])

orphans = []
for cml in PRX.rglob("CMakeLists.txt"):
    base = cml.parent
    claimed = claimed_paths(cml)
    for cpp in base.rglob("*.cpp"):
        parts = cpp.parts
        if "build" in parts or "tests" in parts:
            continue
        # a nested CMakeLists owns its own subtree
        if any((p / "CMakeLists.txt").exists() for p in cpp.parents
               if p != base and base in p.parents):
            continue
        rel = cpp.relative_to(base).as_posix()
        if rel not in claimed and cpp.name not in claimed and cpp.name not in INCLUDED_CPP:
            orphans.append((cml.relative_to(ROOT), cpp.relative_to(ROOT)))

if not orphans:
    print("clean: every .cpp under core/libs/prx is claimed by a CMake target")
else:
    print(f"{len(orphans)} ORPHANED source file(s) -- on disk, in no target:\n")
    for cml, cpp in sorted(orphans):
        print(f"  {cpp}\n      not listed in {cml}")
sys.exit(1 if orphans else 0)
