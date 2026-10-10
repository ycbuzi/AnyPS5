# Extract every file the PC session created/edited with Write/Edit (full contents) from saved
# list_events pages, plus bash heredoc file creations. usage: extract_tools.py <outdir> <pages...>
import sys, json, re, os, ntpath
out = sys.argv[1]; pages = sys.argv[2:]
found = {}
for f in pages:
    s = open(f).read()
    try:
        d = json.loads(s[s.find('{'):s.rfind('}') + 1])['ccr']
    except Exception as e:
        print("skip", f, e); continue
    for ev in d['data']:
        kind = [k for k in ev if k != 'created_at'][0]
        m = ev[kind]
        inner = m.get('internal_anthropic_catchall', m)
        msg = inner.get('message', m.get('message'))
        if not isinstance(msg, dict) or msg.get('role') != 'assistant': continue
        ts = inner.get('timestamp', ev['created_at'])
        for b in msg.get('content') or []:
            if b.get('type') != 'tool_use': continue
            inp = b.get('input') or {}
            if b['name'] == 'Write' and inp.get('file_path'):
                found.setdefault(inp['file_path'], []).append((ts, 'Write', inp.get('content', '')))
            elif b['name'] in ('Edit', 'MultiEdit') and inp.get('file_path'):
                found.setdefault(inp['file_path'], []).append((ts, 'Edit', json.dumps({k: inp.get(k) for k in ('old_string', 'new_string', 'replace_all')})))
            elif b['name'] in ('Bash', 'PowerShell'):
                cmd = inp.get('command', '')
                for hm in re.finditer(r"cat\s*>\s*['\"]?([^\s'\"<]+)['\"]?\s*<<\s*'?(\w+)'?\n(.*?)\n\2\b", cmd, re.S):
                    found.setdefault(hm.group(1), []).append((ts, 'heredoc', hm.group(3)))
for path, versions in sorted(found.items(), key=lambda kv: kv[1][-1][0]):
    versions.sort()
    last_write = [v for v in versions if v[1] in ('Write', 'heredoc')]
    base = ntpath.basename(path.replace('/', '\\'))
    kinds = ",".join(sorted({v[1] for v in versions}))
    print(f"{versions[0][0][:16]}  {len(versions):2d}x {kinds:14s} {path}")
    if last_write:
        with open(os.path.join(out, base), 'w') as fh: fh.write(last_write[-1][2])
