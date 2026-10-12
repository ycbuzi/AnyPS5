import json,re,io,sys
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
ev=json.load(open('evidence.json',encoding='utf-8'))
pid=sys.argv[1]; filt=sys.argv[2] if len(sys.argv)>2 else None; LIM=int(sys.argv[3]) if len(sys.argv)>3 else 2500; OFF=int(sys.argv[4]) if len(sys.argv)>4 else 0
seen=set()
for r in ev.get(pid,[]):
    tag=f"{r['src']}#{r['num']}"
    if filt and filt not in tag: continue
    k=(tag,r['kind'],r['win'][:60])
    if k in seen: continue
    seen.add(k)
    print(f"--- {tag} [{r['kind']}] {r['url']}")
    print(r['win'][OFF:OFF+LIM].replace('\r',''));print()
