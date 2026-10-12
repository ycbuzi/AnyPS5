import json,re,sys,io
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
ev=json.load(open('evidence.json',encoding='utf-8'))
STRONG=re.compile(r'in[- ]game|playable|reaches gameplay|plays\b|runs\b|walking|E1M1|map loaded|level',re.I)
for pid in sorted(ev):
    if not re.match(r'PPSA(9[789]|68|97)',pid): continue
    found=[]
    for r in ev[pid]:
        for s in re.split(r'(?<=[.!?])\s+|\n|\| ',r['win']):
            if pid in s and STRONG.search(s):
                found.append((r['src'],r['num'],re.sub(r'\s+',' ',s)[:240]))
    print(f"### {pid} docs={len(ev[pid])} strong={len(found)}")
    for f in found[:3]: print('    ',f[0]+'#'+str(f[1])+':',f[2])
