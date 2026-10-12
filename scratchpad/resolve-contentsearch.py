"""Resolve libSceContentSearch/Export.cpp.

Upstream's side of the hunk is ten functions: five are NotImplemented stubs for exports we
already implement for real further down this same file, one is a sceContentSearchTerm stub
written against a g_initialized atomic that does not exist in the merged file, and four are
genuinely new. Taking upstream wholesale would downgrade five working implementations to
stubs and fail to compile; a blind union would define five functions twice.
"""
import pathlib
import re

path = pathlib.Path("D:/AnyPS5/core/libs/prx/libSceContentSearch/Export.cpp")
body = path.read_text(encoding="utf-8", errors="replace")

NEW = """int APS5_VABI sceContentSearchGetContentLastUpdateId(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceContentSearchOpenMetadataByContentId(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceContentSearchGetTotalContentSize(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceContentSearchGetNumOfContent(void) {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

"""

match = re.search(r"<<<<<<< HEAD\r?\n(.*?)=======\r?\n.*?>>>>>>> origin/main\r?\n", body, re.S)
if not match:
    raise SystemExit("no conflict found")

# keep our Term, and put the four genuinely new stubs ahead of it
body = body[:match.start()] + NEW + match.group(1) + body[match.end():]
path.write_text(body, encoding="utf-8")
print(f"markers left: {body.count('<<<<<<<')}")
for name in ("sceContentSearchCloseMetadata", "sceContentSearchSearchContent",
             "sceContentSearchTerm", "sceContentSearchGetNumOfContent"):
    print(f"  {name}: {len(re.findall(r'APS5_VABI ' + name + r'\b', body))} definition(s)")
