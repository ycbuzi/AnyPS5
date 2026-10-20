# usage: python3 -I summaries.py <saved list_events file>...  -> prints every compaction summary in full
import sys, json
for f in sys.argv[1:]:
    s = open(f).read()
    d = json.loads(s[s.find('{'):s.rfind('}') + 1])['ccr']
    for ev in d['data']:
        kind = [k for k in ev if k != 'created_at'][0]
        m = ev[kind]
        msg = m.get('internal_anthropic_catchall', m).get('message', m.get('message'))
        if not isinstance(msg, dict) or msg.get('role') != 'user':
            continue
        c = msg.get('content')
        blocks = [{'type': 'text', 'text': c}] if isinstance(c, str) else (c or [])
        for b in blocks:
            if b.get('type') == 'text' and 'being continued from a previous conversation' in b.get('text', ''):
                print("=" * 30, f.rsplit('-', 1)[-1], len(b['text']), "chars")
                print(b['text'])
