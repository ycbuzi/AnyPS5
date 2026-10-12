"""Resolve core/libs/SceTypes.hpp.

ContentExportParam's third field is `reserved` on our side and `comment` upstream. Upstream's
name is the accurate one and matching it keeps this struct from conflicting again, but our
side also carries three `using` declarations that libSceContentExport/Export.cpp actually
uses, plus an extra offsetof assert. So: upstream's field name, our declarations, both
asserts. Our Export.cpp reference to `param.reserved` is renamed separately.
"""
import pathlib
import re

path = pathlib.Path("D:/AnyPS5/core/libs/SceTypes.hpp")
body = path.read_text(encoding="utf-8", errors="replace")

REPLACEMENT = """    char comment[257];
    char content_type[65];
};

using ContentExportMalloc = void* (APS5_VABI*)(std::size_t size, void* userData);
using ContentExportFree = void (APS5_VABI*)(void* pointer, void* userData);
using ContentExportDataProvideFunction = int (APS5_VABI*)(void** data, std::size_t* size, void* userData);

static_assert(sizeof(ContentExportParam) == 579);
static_assert(offsetof(ContentExportParam, content_type) == 514);
"""

pattern = re.compile(r"<<<<<<< HEAD\r?\n.*?=======\r?\n.*?>>>>>>> origin/main\r?\n", re.S)
body, count = pattern.subn(REPLACEMENT, body, count=1)
path.write_text(body, encoding="utf-8")
print(f"replaced {count} hunk(s); markers left: {body.count('<<<<<<<')}")
for token in ("char comment[257]", "ContentExportMalloc", "offsetof(ContentExportParam"):
    print(f"  {token!r}: {body.count(token)}")
