import json,re,io,sys,collections
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
ev=json.load(open('evidence.json',encoding='utf-8'))
TARGETS="""PPSA01325 PPSA01341 PPSA01342 PPSA02929 PPSA03030 PPSA03306 PPSA03647 PPSA03706 PPSA04264
PPSA05324 PPSA05684 PPSA06399 PPSA06699 PPSA07286 PPSA12544 PPSA15388 PPSA15657 PPSA18605 PPSA19639
PPSA19728 PPSA19943 PPSA21402 PPSA21564 PPSA21567 PPSA21703 PPSA22520 PPSA28997 PPSA29413 PPSA36082
PPSA99007 PPSA99666""".split()
for p in TARGETS:
    rs=ev.get(p,[])
    locs=collections.Counter((r['src'],r['num']) for r in rs)
    print(f"{p}: {len(rs)} recs; threads: "+", ".join(f"{s}#{n}x{c}" for (s,n),c in locs.most_common()))
