# usage: python3 -I conv.py <saved list_events file> [max_assistant_chars]
import sys, json
s = open(sys.argv[1]).read()
amax = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
d = json.loads(s[s.find('{'):s.rfind('}') + 1])['ccr']
print("CURSOR", {k: v for k, v in d.items() if k != 'data'}, "events", len(d['data']))
for ev in d['data']:
    kind = [k for k in ev if k != 'created_at'][0]
    m = ev[kind]
    msg = m.get('internal_anthropic_catchall', m).get('message', m.get('message'))
    if not isinstance(msg, dict):
        continue
    c = msg.get('content')
    blocks = [{'type': 'text', 'text': c}] if isinstance(c, str) else (c or [])
    for b in blocks:
        t = b.get('type')
        if t == 'text':
            txt = b['text'].strip()
            if not txt:
                continue
            if msg.get('role') == 'user':
                print("\n>>> USER:", txt[:3000])
            else:
                print("\n    CLAUDE:", txt[:amax] + (" [...]" if len(txt) > amax else ""))
        elif t == 'tool_use' and msg.get('role') == 'assistant':
            desc = (b.get('input') or {}).get('description')
            if desc:
                print("      [tool:", desc[:100], "]")
