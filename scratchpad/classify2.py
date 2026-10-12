import json, re, os
from collections import defaultdict
HERE = os.path.dirname(os.path.abspath(__file__))
def J(n): return json.load(open(os.path.join(HERE,n),encoding='utf-8'))
PPSA = re.compile(r'PPSA\d{5}')

docs=[]
def add(**k): docs.append(k)
for d in J('discussions.json'):
    head=(d['title'] or '')+'\n'+(d['body'] or '')
    parent=sorted(set(PPSA.findall(d['title'] or ''))) or sorted(set(PPSA.findall(head)))
    add(src='D',num=d['number'],kind='body',cat=d['category']['name'],title=d['title'],text=head,url=d['url'],inherit=parent)
    for c in d['comments']['nodes']:
        add(src='D',num=d['number'],kind='comment',cat=d['category']['name'],title=d['title'],text=c['body'] or '',url=c['url'],inherit=parent)
        for r in c['replies']['nodes']:
            add(src='D',num=d['number'],kind='reply',cat=d['category']['name'],title=d['title'],text=r['body'] or '',url=r['url'],inherit=parent)
for i in J('issues.json'):
    head=(i['title'] or '')+'\n'+(i['body'] or '')
    parent=sorted(set(PPSA.findall(i['title'] or '')))
    add(src='I',num=i['number'],kind='body',cat='issue',title=i['title'],text=head,url=i['url'],inherit=parent)
    for c in i['comments']['nodes']:
        add(src='I',num=i['number'],kind='comment',cat='issue',title=i['title'],text=c['body'] or '',url=c['url'],inherit=parent)
for c in J('i2523_extra.json'):
    add(src='I',num=2523,kind='comment',cat='issue',title='(issue 2523)',text=c['body'] or '',url=c['url'],inherit=[])

# docs that have NO ppsa of their own but inherit exactly one -> attributable
STRONG=re.compile(r"in[- ]game|playable|gameplay|walking around|responds to the pad|plays through|first level|completed a level|completed the first|into the tutorial|plays? (?:a |the )?(?:level|hole|match|course)|loads a game|new game",re.I)
out=defaultdict(list)
for d in docs:
    own=sorted(set(PPSA.findall(d['text'])))
    if own: continue
    if len(d['inherit'])!=1: continue
    if not STRONG.search(d['text']): continue
    out[d['inherit'][0]].append((d['src'],d['num'],d['kind'],d['text']))
print('inherited-attribution docs with strong wording:',sum(len(v) for v in out.values()),'ids:',len(out))
json.dump({k:[(a,b,c,t) for a,b,c,t in v] for k,v in out.items()},open(os.path.join(HERE,'inherited.json'),'w',encoding='utf-8'),ensure_ascii=False)
print(sorted(out))
