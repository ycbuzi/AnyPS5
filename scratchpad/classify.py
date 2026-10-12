import json, re, os
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
def J(n): return json.load(open(os.path.join(HERE, n), encoding='utf-8'))

PPSA = re.compile(r'PPSA\d{5}')

# ---- build document corpus -------------------------------------------------
docs = []  # {src, num, kind, title, text, url}
for d in J('discussions.json'):
    docs.append(dict(src='D', num=d['number'], kind='body', cat=d['category']['name'],
                     title=d['title'], text=(d['title'] or '') + '\n' + (d['body'] or ''), url=d['url']))
    for c in d['comments']['nodes']:
        docs.append(dict(src='D', num=d['number'], kind='comment', cat=d['category']['name'],
                         title=d['title'], text=c['body'] or '', url=c['url']))
        for r in c['replies']['nodes']:
            docs.append(dict(src='D', num=d['number'], kind='reply', cat=d['category']['name'],
                             title=d['title'], text=r['body'] or '', url=r['url']))
for i in J('issues.json'):
    docs.append(dict(src='I', num=i['number'], kind='body', cat='issue',
                     title=i['title'], text=(i['title'] or '') + '\n' + (i['body'] or ''), url=i['url']))
    for c in i['comments']['nodes']:
        docs.append(dict(src='I', num=i['number'], kind='comment', cat='issue',
                         title=i['title'], text=c['body'] or '', url=c['url']))
for c in J('i2523_extra.json'):
    docs.append(dict(src='I', num=2523, kind='comment', cat='issue',
                     title='(issue 2523)', text=c['body'] or '', url=c['url']))

# ---- classification -------------------------------------------------------
NEG_PRE = r"(?:not |n't |no |never |isn't |doesn't |does not |without |before |fails? to |failed to |unable to |can't |cannot |short of |instead of |rather than |prior to |yet to )"

POS = [r'in[- ]game', r'playable', r'gameplay', r'in game', r'plays? (?:fine|well|correctly)',
       r'reach(?:es|ed)? (?:the )?(?:first )?(?:level|gameplay|stage)', r'starts? the game',
       r'controll?able', r'moving (?:around|in)', r'first level',
       r'boots and plays', r'plays through', r'walking around', r'plays at \d', r'into the tutorial',
       r'reach(?:es|ed) the tutorial', r'levels render', r'completed the first', r'first (?:hole|mission|match)',
       r'plays? (?:a |the )?(?:level|hole|game|match|course)', r'loads a game', r'new game (?:plays|reaches|starts)',
       r'(?:tutorial|level|world|stage|chapter|zone|hole|mission) (?:plays|loads|reached|renders)', r'3D reached']
MENU = [r'main menu', r'title screen', r'start screen', r'(?:reach|reaches|reached|gets? to|got to|boots? to|shows?)[^.\n]{0,25}menu',
        r'menu (?:is )?(?:renders?|rendered|shows?|shown|visible|appears)', r'intro (?:video|movie|cutscene|logos?)',
        r'splash screen', r'launcher', r'logo screen', r'press (?:any key|start)']
BAD = [r'crash', r'black screen', r'not implemented', r'unimplemented', r'stops? at', r'stops? on',
       r'stub', r'abort', r'hangs?\b', r'freez', r'does not (?:boot|start|run|launch)',
       r"doesn't (?:boot|start|run|launch)", r'segfault', r'exception', r'fatal', r'exit(?:s|ed)? (?:with|early)',
       r'unsupported', r'missing', r'fail', r'error', r'nothing happens', r'no (?:window|output|display)',
       r'infinite loop', r'deadlock', r'blocked', r'stuck']

def hits(pats, s):
    out = []
    for p in pats:
        for m in re.finditer(p, s, re.I):
            a = max(0, m.start() - 40)
            pre = s[a:m.start()].lower()
            negated = re.search(NEG_PRE + r'[\w ,\'"]{0,24}$', pre) is not None
            out.append((m.group(0), negated, m.start()))
    return out

WIN = 1500
recs = defaultdict(list)  # ppsa -> list of evidence records
for doc in docs:
    t = doc['text']
    ms = list(PPSA.finditer(t))
    ids_in_doc = sorted({m.group(0) for m in ms})
    if not ids_in_doc:
        continue
    for pid in ids_in_doc:
        # window = union of +-WIN chars around each occurrence of this id
        spans = [(max(0, m.start() - WIN), min(len(t), m.end() + WIN)) for m in ms if m.group(0) == pid]
        # if the doc is short, just use whole doc
        if len(ids_in_doc) <= 2 or len(t) < 2500:
            win = t
        else:
            merged = []
            for s, e in sorted(spans):
                if merged and s <= merged[-1][1]:
                    merged[-1] = (merged[-1][0], max(merged[-1][1], e))
                else:
                    merged.append((s, e))
            win = '\n...\n'.join(t[s:e] for s, e in merged)
        p = [h for h in hits(POS, win) if not h[1]]
        pn = [h for h in hits(POS, win) if h[1]]
        mn = [h for h in hits(MENU, win) if not h[1]]
        b = hits(BAD, win)
        recs[pid].append(dict(src=doc['src'], num=doc['num'], kind=doc['kind'], cat=doc['cat'],
                              title=doc['title'], url=doc['url'],
                              pos=[h[0] for h in p], pos_neg=[h[0] for h in pn],
                              menu=[h[0] for h in mn], bad=[h[0] for h in b], win=win))

json.dump({k: v for k, v in recs.items()}, open(os.path.join(HERE, 'evidence.json'), 'w', encoding='utf-8'),
          ensure_ascii=False, indent=1)

print('docs with PPSA:', sum(1 for d in docs if PPSA.search(d['text'])), '/', len(docs))
print('distinct PPSA:', len(recs))

cand_pos = sorted(k for k, v in recs.items() if any(r['pos'] for r in v))
cand_menu = sorted(k for k, v in recs.items() if not any(r['pos'] for r in v) and any(r['menu'] for r in v))
print('ids with un-negated POS hit:', len(cand_pos))
print(cand_pos)
print('ids with MENU only:', len(cand_menu))
print(cand_menu)
