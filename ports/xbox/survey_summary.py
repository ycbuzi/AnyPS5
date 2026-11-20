import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def scrub(text):
    host = (HERE / "out" / ".host").read_text().strip()
    if host:
        text = text.replace(host, "<xbox>")
    text = re.sub(r"\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}", "<ip>", text)
    text = re.sub(r"AnyPS5\.[A-Za-z]+_[A-Za-z0-9._~-]+", "<pkg>", text)
    text = re.sub(r"UserMgr[^\\/\"]*", "<user>", text)
    text = re.sub(r"_+[a-z0-9]{13}\b", "_<family>", text)
    return text


def main():
    s = json.loads((HERE / "out" / (sys.argv[1] if len(sys.argv) > 1 else "survey.json")).read_text(encoding="utf-8"))
    out = []
    out.append("os %s, logical cpus %s" % (s["os"].get("version"), s["os"].get("logical")))
    c = s.get("cpu", {})
    out.append("cpu %s | avx %s avx2 %s fma %s | affinity %s | cpu sets %s | single %s Mops | scaling %s | sleep(1) %s ms" % (c.get("brand", "").strip(), c.get("avx"), c.get("avx2"), c.get("fma"), c.get("affinity"), c.get("cpu_sets"), c.get("single_mops"), c.get("scaling_vs_one"), c.get("sleep1_ms")))
    out.append("memory %s" % s.get("memory"))
    g = s.get("gpu", {})
    out.append("gpu fl %s sm %s options1 %s uma %s va %s compute queue %s timestamps %s hz, query %s, calibration %s" % (g.get("feature_level"), g.get("shader_model"), g.get("options1"), g.get("uma"), g.get("va_bits"), g.get("compute_queue"), g.get("timestamp_hz"), g.get("timestamp_query"), g.get("clock_calibration")))
    out.append("gpu options %s" % g.get("options"))
    out.append("adapter %s" % g.get("adapter"))
    formats = g.get("formats", {})
    noload = [k for k, v in formats.items() if "uav" in v and "load" not in v]
    out.append("typed UAV: %d formats, load OK on %s; NO load on %s" % (len(formats), [k for k, v in formats.items() if "load" in v], noload))
    st = s.get("storage", {})
    out.append("drives %s" % st.get("drives"))
    for name, listing in st.get("listings", {}).items():
        out.append("  list %-20s %s" % (name, listing.get("error") if "error" in listing else "%d entries %s" % (listing["count"], listing["names"][:8])))
    out.append("localstate free %s GB, write %s MB/s, read %s MB/s" % (st.get("local_free_gb"), st.get("local_write_mb_s"), st.get("local_read_mb_s")))
    api = s.get("api", {})
    loaded = {k: v["exports"] for k, v in api.items() if isinstance(v, dict) and "exports" in v}
    missing = {k: v["error"] for k, v in api.items() if isinstance(v, dict) and "error" in v}
    out.append("dlls loaded (exports) %s" % loaded)
    out.append("dlls missing (error) %s" % missing)
    out.append("system32 dlls %s" % api.get("_system32_dlls"))
    out.append("network %s" % s.get("network"))
    print(scrub("\n".join(out)))


if __name__ == "__main__":
    main()
