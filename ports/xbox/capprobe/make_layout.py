import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SRC = ROOT / "out" / "layout"
DST = ROOT / "out" / "caplayout"

CAPS = {
    "base": ["codeGeneration", "internetClientServer", "privateNetworkClientServer"],
    "uap": ["removableStorage", "documentsLibrary", "picturesLibrary", "videosLibrary", "musicLibrary"],
    "rescap": ["broadFileSystemAccess", "appDiagnostics", "packageManagement", "packageQuery", "expandedResources",
               "extendedExecutionUnconstrained", "confirmAppClose", "xboxAccessoryManagement"],
}


def main():
    drop = set(sys.argv[1:])
    if DST.exists():
        shutil.rmtree(DST)
    shutil.copytree(SRC, DST, ignore=shutil.ignore_patterns("dxbc", "resources.pri"))
    manifest = (ROOT / "AppxManifest.xml").read_text(encoding="utf-8")
    manifest = manifest.replace('Name="AnyPS5.XboxProbe"', 'Name="AnyPS5.CapProbe"', 1)
    manifest = manifest.replace('DisplayName="AnyPS5 Probe"', 'DisplayName="AnyPS5 Cap Probe"')
    manifest = re.sub(r"<DisplayName>[^<]*</DisplayName>", "<DisplayName>AnyPS5 Cap Probe</DisplayName>", manifest)
    lines = ["  <Capabilities>"]
    for prefix, names in CAPS.items():
        for name in names:
            if name in drop:
                continue
            tag = "Capability" if prefix == "base" else prefix + ":Capability"
            lines.append('    <%s Name="%s" />' % (tag, name))
    lines.append("  </Capabilities>")
    manifest = re.sub(r"  <Capabilities>.*?</Capabilities>", "\n".join(lines), manifest, flags=re.S)
    (DST / "AppxManifest.xml").write_text(manifest, encoding="utf-8")
    print("layout ready, caps:", sum(len(v) for v in CAPS.values()) - len(drop))


if __name__ == "__main__":
    main()
