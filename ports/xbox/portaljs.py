import re, sys
import deploy
call = deploy.portal(sys.argv[1])
seen = set()
def get(path):
    try:
        return call("GET", path).decode("utf-8", "replace")
    except Exception as e:
        return ""
pages = ["/", "/default.htm", "/index.html"]
texts = {}
for p in pages:
    t = get(p)
    if t:
        texts[p] = t
        for s in re.findall(r'src="([^"]+\.js[^"]*)"', t):
            if s not in seen:
                seen.add(s)
                texts[s] = get(s if s.startswith("/") else "/" + s)
pat = re.compile(r'.{0,80}(?i:(contenttype|apptype|titletype|"game"|/api/app/[a-z/]+|/ext/app[a-z/]*)).{0,80}')
hits = set()
for k, t in texts.items():
    for m in pat.finditer(t):
        hits.add(m.group(0).strip()[:200])
print(len(texts), "files")
for h in sorted(hits)[:60]:
    print(h)
