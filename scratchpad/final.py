import json,re,io,sys,collections
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
ev=json.load(open('evidence.json',encoding='utf-8'))
GAMEPLAY="""PPSA01341 PPSA02929 PPSA03030 PPSA03306 PPSA03647 PPSA03706 PPSA04264 PPSA05324
PPSA05684 PPSA06399 PPSA06699 PPSA12544 PPSA15388 PPSA15657 PPSA18605 PPSA19639 PPSA19728 PPSA19943
PPSA21402 PPSA21564 PPSA21567 PPSA21703 PPSA22520 PPSA28997 PPSA29413 PPSA36082 PPSA99007 PPSA99666""".split()
BORDER=['PPSA10112']
MENU="""PPSA01342 PPSA01599 PPSA02100 PPSA03355 PPSA05347 PPSA08427 PPSA11142 PPSA13579 PPSA14708
PPSA18176 PPSA23126 PPSA25344 PPSA26344 PPSA26358 PPSA28416 PPSA29343 PPSA34063
PPSA99902 PPSA99003 PPSA99000 PPSA99005 PPSA99050 PPSA98001 PPSA39410 PPSA97510 PPSA98510 PPSA69420""".split()
MENTION="""PPSA17221 PPSA01530 PPSA12338 PPSA25573 PPSA15421 PPSA23566 PPSA04610 PPSA06320 PPSA01289
PPSA01736 PPSA28319 PPSA77711 PPSA85300 PPSA92280 PPSA99141 PPSA99109 PPSA99110 PPSA99920 PPSA99930
PPSA99169 PPSA99764 PPSA15186 PPSA07631 PPSA99611 PPSA99773 PPSA68091 PPSA01748 PPSA99001 PPSA99002""".split()
allids=sorted(ev)
rest=[p for p in allids if p not in set(GAMEPLAY+BORDER+MENU+MENTION)]
def hb(p): return p.startswith('PPSA99')
def name(pid):
    c=collections.Counter()
    for r in ev[pid]:
        for m in re.finditer(r"([A-Z][\w:'’!&.\-]*(?: [A-Z0-9][\w:'’!&.\-]*){0,5})\s*\(\s*`?"+pid,r['win']): c[m.group(1).strip()]+=2
        for m in re.finditer(pid+r"[^)\n]{0,20}\)?[,:]?\s+(?:is\s+)?([A-Z][\w:'’!&.\-]*(?: [A-Z0-9][\w:'’!&.\-]*){0,4})",r['win']): c[m.group(1).strip()]+=1
    bad=re.compile(r'^(Title|Game|Update|Host|Build|Status|Windows|Linux|Tested|Both|With|The|And|Its|A |I |It|Unreal|Unity|Engine|FPS|On |No |AI|PR|PPSA)')
    for n,_ in c.most_common(12):
        if not bad.match(n) and len(n)>2: return n
    return '?'
def counts(lst):
    h=[p for p in lst if hb(p)]; return len(lst),len(lst)-len(h),len(h)
print('TOTAL distinct PPSA:',len(allids),' retail(non-99xxx):',len([p for p in allids if not hb(p)]),' PPSA99xxx:',len([p for p in allids if hb(p)]))
for lbl,lst in [('GAMEPLAY',GAMEPLAY),('BORDERLINE',BORDER),('MENU/BOOT only',MENU),('CRASH/STOP only',rest),('MENTION-ONLY/UNCLEAR',MENTION)]:
    t,r,h=counts(lst); print(f'{lbl}: total={t} retail={r} PPSA99xxx={h}')
print()
print('--- gameplay table ---')
for p in GAMEPLAY+BORDER:
    print(f'{p} | {name(p)}')
