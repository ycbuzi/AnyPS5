import json,re,sys,io
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
ev=json.load(open('evidence.json',encoding='utf-8'))
for pid in sys.argv[1:]:
    print('#'*90); print('###',pid)
    for r in ev[pid]:
        w=r['win']
        # show only sentences containing the id or strong verdict words
        sents=re.split(r'(?<=[.!?])\s+|\n',w)
        keep=[s for s in sents if re.search(pid+r'|playable|in[- ]game|gameplay|Status|stops|stop at|boots|menu|level|tutorial|not implemented',s,re.I)]
        if not keep: continue
        print(f"-- {r['src']}#{r['num']} [{r['kind']}] {r['title'][:80]}")
        for s in keep[:14]:
            print('    *',re.sub(r'\s+',' ',s)[:300])
