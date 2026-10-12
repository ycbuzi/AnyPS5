import json,re,os,sys
HERE=os.path.dirname(os.path.abspath(__file__))
ev=json.load(open(os.path.join(HERE,'evidence.json'),encoding='utf-8'))
POS=[r'in[- ]game',r'playable',r'gameplay',r'in game',r'plays? (?:fine|well|correctly)',
     r'reach(?:es|ed)? (?:the )?(?:first )?(?:level|gameplay|stage)',r'starts? the game',
     r'controll?able',r'moving (?:around|in)',r'first level']
ids=sys.argv[1:] if len(sys.argv)>1 else sorted(k for k,v in ev.items() if any(r['pos'] for r in v))
out=[]
for pid in ids:
    out.append('='*100); out.append('### '+pid)
    for r in ev[pid]:
        if not r['pos']: continue
        out.append(f"--- {r['src']}#{r['num']} [{r['kind']}/{r['cat']}] {r['title'][:90]}")
        out.append(f"    POSHITS={r['pos']} BAD={sorted(set(r['bad']))[:8]}")
        w=r['win']
        for p in POS:
            for m in re.finditer(p,w,re.I):
                a=max(0,m.start()-320); b=min(len(w),m.end()+320)
                out.append('    >> '+re.sub(r'\s+',' ',w[a:b]))
        out.append('')
open(os.path.join(HERE,'review_pos.txt'),'w',encoding='utf-8').write('\n'.join(out))
print(len('\n'.join(out)))
