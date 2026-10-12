import json,re,os,io,sys
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
from collections import defaultdict
HERE=os.path.dirname(os.path.abspath(__file__))
def J(n): return json.load(open(os.path.join(HERE,n),encoding='utf-8'))
PPSA=re.compile(r'PPSA\d{5}')
docs=[]
for d in J('discussions.json'):
    head=(d['title'] or '')+'\n'+(d['body'] or '')
    par=sorted(set(PPSA.findall(d['title'] or ''))) or sorted(set(PPSA.findall(head)))
    for c in d['comments']['nodes']:
        docs.append(('D',d['number'],c['body'] or '',par))
        for r in c['replies']['nodes']: docs.append(('D',d['number'],r['body'] or '',par))
for i in J('issues.json'):
    par=sorted(set(PPSA.findall(i['title'] or '')))
    for c in i['comments']['nodes']: docs.append(('I',i['number'],c['body'] or '',par))
BROAD=re.compile(r"(?:runs|ran|plays|played|playing|completed|finished|beat|progress(?:ed)?|reached)\b[^.\n]{0,70}"
                 r"(?:level|mission|stage|chapter|hole|match|course|tutorial|boss|world|map|area|room|minutes? of|hours? of|the game|correctly|perfectly|fine|well)",re.I)
known=set("""PPSA01325 PPSA01341 PPSA02929 PPSA03030 PPSA03306 PPSA03647 PPSA03706 PPSA04264 PPSA05324
PPSA05684 PPSA06399 PPSA06699 PPSA12544 PPSA15388 PPSA15657 PPSA18605 PPSA19639 PPSA19728 PPSA19943
PPSA21402 PPSA21564 PPSA21567 PPSA21703 PPSA22520 PPSA28997 PPSA29413 PPSA36082 PPSA99007 PPSA99666 PPSA10112""".split())
out=defaultdict(list)
for src,num,t,par in docs:
    own=sorted(set(PPSA.findall(t)))
    ids=own or par
    if len(ids)!=1: continue
    if ids[0] in known: continue
    for m in BROAD.finditer(t):
        out[ids[0]].append((src,num,re.sub(r'\s+',' ',t[max(0,m.start()-200):m.start()+220])))
for pid in sorted(out):
    print('###',pid)
    for h in out[pid][:2]: print(f'   {h[0]}#{h[1]}: {h[2][:360]}')
