import json,re,io,sys
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace',newline='\n')
kind=sys.argv[1]; num=int(sys.argv[2]); LIM=int(sys.argv[3]); TAIL=int(sys.argv[4])
f='discussions.json' if kind=='D' else 'issues.json'
for d in json.load(open(f,encoding='utf-8')):
    if d['number']!=num: continue
    items=[('body',d['body'] or '',d['url'])]
    for c in d['comments']['nodes']:
        items.append(('comment',c['body'] or '',c['url']))
        for r in c.get('replies',{}).get('nodes',[]): items.append(('reply',r['body'] or '',r['url']))
    for k,b,u in items[-TAIL:]:
        print(f'--- [{k}] {u}'); print(b[:LIM].replace('\r','')); print()
