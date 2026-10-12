import json,re,sys,io
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
d=json.load(open('discussions.json',encoding='utf-8'))
for n in d:
    if n['category']['name']!='Game bring-up': continue
    b=n['body'] or ''
    m=re.search(r'#+\s*Status\b(.{0,420})',b,re.S|re.I)
    st=re.sub(r'\s+',' ',m.group(1)).strip() if m else re.sub(r'\s+',' ',b[:300])
    ids=sorted(set(re.findall(r'PPSA\d{5}',n['title']+' '+b)))
    print(f"D#{n['number']} | {n['title'][:72]}")
    print(f"   ids={ids[:4]} ncomments={n['comments']['totalCount']}")
    print(f"   STATUS{'' if m else '(no Status hdr; body head)'}: {st[:400]}")
    print()
