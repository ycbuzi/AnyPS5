"""Give our wcstok block its own scope in GuestWideStrings.cpp.

Both sides of the merge test wcstok and both declare `tokens` and `token` in main(), so the
union left two declarations in one scope. Both tests are worth keeping -- ours drives the
saved state through `state`, upstream's through `last` -- so the fix is a nested scope, not
deleting a test. Done as a script because the block contains non-ASCII literals that a
literal-text edit would risk re-encoding.
"""
import pathlib

path = pathlib.Path("D:/AnyPS5/core/libs/tests/GuestWideStrings.cpp")
lines = path.read_text(encoding="utf-8", errors="strict").splitlines(keepends=True)

start, end = 122, 134  # 1-indexed, inclusive: our tokens/state/token block
assert "char16_t tokens[]" in lines[start - 1], lines[start - 1]
assert "&state) == nullptr)" in lines[end - 1], lines[end - 1]

out = (lines[:start - 1]
       + ["    {   // scope: upstream's wcstok test below declares `tokens` and `token` too\n"]
       + ["    " + line if line.strip() else line for line in lines[start - 1:end]]
       + ["    }\n"]
       + lines[end:])
path.write_text("".join(out), encoding="utf-8")
print(f"wrapped lines {start}-{end}; file now {len(out)} lines")
