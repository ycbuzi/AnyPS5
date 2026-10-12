import json,re,io,sys
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
ev=json.load(open('evidence.json',encoding='utf-8'))
args=sys.argv[1:]
LIM=int(args[0]); ids=args[1:]
for p in ids:
    print('='*30,p)
    seen=set()
    for r in ev.get(p,[]):
        key=(r['src'],r['num'],r['kind'],r['win'][:80])
        if key in seen: continue
        seen.add(key)
        w=r['win']
        print(f"--- {r['src']}#{r['num']} [{r['kind']}] cat={r['cat']} title={r['title'][:70]!r}")
        print(f"    url={r['url']}")
        print(w[:LIM].replace('\r',''))
        print()
