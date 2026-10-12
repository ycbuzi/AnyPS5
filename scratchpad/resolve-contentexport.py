"""Resolve libSceContentExport/Export.cpp.

Our side is a complete implementation: file and callback copying, PNG/JPEG/GIF decoding,
catalog transactions, thumbnails. Upstream's side is a session-tracking skeleton built on
g_initialized/g_sessions whose export bodies mostly throw "the host has no media library".
Taking upstream would delete a working implementation, so ours wins.

Ours is missing three exports upstream has (Cancel, GetProgress, FromDataWithThumbnail).
Their bodies depend on upstream's g_sessionMutex/RequireSession, which do not exist on our
side, so they are added as NotImplemented stubs instead: export coverage preserved, nothing
that fails to compile.

Also renames param.reserved -> param.comment to follow SceTypes.hpp, where upstream's field
name was taken.
"""
import pathlib
import re

path = pathlib.Path("D:/AnyPS5/core/libs/prx/libSceContentExport/Export.cpp")
body = path.read_text(encoding="utf-8", errors="replace")

HUNK = re.compile(r"<<<<<<< HEAD\r?\n(.*?)=======\r?\n.*?>>>>>>> origin/main\r?\n", re.S)
body, hunks = HUNK.subn(lambda m: m.group(1), body)
print(f"kept ours for {hunks} hunk(s); markers left: {body.count('<<<<<<<')}")

body, renamed = re.subn(r"param\.reserved\b", "param.comment", body)
print(f"renamed param.reserved -> param.comment: {renamed}")

STUBS = """
int APS5_VABI sceContentExportGetProgress(std::int32_t expId) {
    (void)expId;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceContentExportCancel(std::int32_t expId) {
    (void)expId;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceContentExportFromDataWithThumbnail(std::int32_t expId, const ContentExportParam* param,
    const ContentExportCallbackParam* contentCallbackParam, const char* thumbnailType,
    const ContentExportCallbackParam* thumbnailCallbackParam, char* path, std::size_t pathBufferLength) {
    (void)expId; (void)param; (void)contentCallbackParam; (void)thumbnailType;
    (void)thumbnailCallbackParam; (void)path; (void)pathBufferLength;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}
"""

# append inside the file's final extern "C" / namespace close: put before the last closing brace
index = body.rstrip().rfind("\n}")
if index == -1:
    body = body.rstrip() + "\n" + STUBS
else:
    body = body[:index + 1] + STUBS + body[index + 1:]

path.write_text(body, encoding="utf-8")
for name in ("sceContentExportGetProgress", "sceContentExportCancel",
             "sceContentExportFromDataWithThumbnail", "sceContentExportStart"):
    print(f"  {name}: {len(re.findall(r'APS5_VABI\s+' + name + r'\s*\(', body))} definition(s)")
