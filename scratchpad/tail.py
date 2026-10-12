import json,re,sys,io
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
ev=json.load(open('evidence.json',encoding='utf-8'))
POSW=re.compile(r'in[- ]game|playable|gameplay|plays|walking|tutorial|first level|level|match|hole|mission|runs at|fps',re.I)
done=set(sys.argv[1:])
for pid in sorted(ev):
    if done and pid not in done: continue
    sents=[]
    for r in ev[pid]:
        for s in re.split(r'(?<=[.!?])\s+|\n|\| ',r['win']):
            if pid in s:
                sents.append((r['src'],r['num'],re.sub(r'\s+',' ',s)[:230]))
    # prefer sentences with positive-ish words
    pri=[s for s in sents if POSW.search(s[2])]
    use=(pri or sents)[:3]
    print(f"### {pid}  (docs={len(ev[pid])})")
    for src,num,s in use: print(f"    {src}#{num}: {s}")
