import json,re,io,sys
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
ev=json.load(open('evidence.json',encoding='utf-8'))
GAMEPLAY=set("""PPSA01341 PPSA02929 PPSA03030 PPSA03306 PPSA03647 PPSA03706 PPSA04264 PPSA05324
PPSA05684 PPSA06399 PPSA06699 PPSA12544 PPSA15388 PPSA15657 PPSA18605 PPSA19639 PPSA19728 PPSA19943
PPSA21402 PPSA21564 PPSA21567 PPSA21703 PPSA22520 PPSA28997 PPSA29413 PPSA36082 PPSA99007 PPSA99666""".split())
AFF=re.compile(r"(reach(?:es|ed)?|boots? to|gets? to|got to|shows?|showed|draws?|drew|renders?|rendered|displays?|opens?|opened|arrives? at|past)"
               r"[^.;\n]{0,60}(main menu|title screen|start screen|menu|complete UI|UI|language menu|title/start screen|PRESS START|press start|character creation|calibration screen|splash|first real UI|world map|loading screen)",re.I)
NEG=re.compile(r"\b(not|never|no|doesn't|does not|fails? to|cannot|can't|without|before)\b[^.;\n]{0,30}$",re.I)
for pid in sorted(ev):
    if pid in GAMEPLAY: continue
    hits=[]
    for r in ev[pid]:
        for s in re.split(r'(?<=[.!?])\s+|\n|\| ',r['win']):
            s=re.sub(r'\s+',' ',s).strip()
            for m in AFF.finditer(s):
                if NEG.search(s[:m.start()]): continue
                hits.append((r['src'],r['num'],s[:200]))
    if hits:
        print(f"### {pid}")
        seen=set()
        for h in hits:
            if h[2][:60] in seen: continue
            seen.add(h[2][:60]); print(f"   {h[0]}#{h[1]}: {h[2]}")
