import re,sys,io
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
t=open('review_pos.txt',encoding='utf-8').read()
blocks=t.split('='*100)
first=sys.argv[1]; n=int(sys.argv[2]) if len(sys.argv)>2 else 8
start=0
for i,b in enumerate(blocks):
    m=re.search(r'### (PPSA\d+)',b)
    if m and m.group(1)==first: start=i;break
# dedupe >> lines per block, trim
for b in blocks[start:start+n]:
    seen=set();out=[]
    for ln in b.split('\n'):
        s=ln.strip()
        if s.startswith('>>'):
            key=s[:120]
            if key in seen: continue
            seen.add(key)
            ln=ln[:460]
        out.append(ln)
    print('\n'.join(out)); print('='*60)
