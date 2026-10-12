import json,re,io,sys
sys.stdout=io.TextIOWrapper(sys.stdout.buffer,encoding='utf-8',errors='replace')
kind=sys.argv[1]; num=int(sys.argv[2]); LIM=int(sys.argv[3]) if len(sys.argv)>3 else 1200
pat=sys.argv[4] if len(sys.argv)>4 else None
f='discussions.json' if kind=='D' else 'issues.json'
for d in json.load(open(f,encoding='utf-8')):
    if d['number']!=num: continue
    print('TITLE:',d['title']);print('URL:',d['url'])
    items=[('body',d['body'] or '',d['url'])]
    for c in d['comments']['nodes']:
        items.append(('comment',c['body'] or '',c['url']))
        for r in c.get('replies',{}).get('nodes',[]):
            items.append(('reply',r['body'] or '',r['url']))
    print('ITEMS:',len(items))
    for k,b,u in items:
        if pat and not re.search(pat,b,re.I): continue
        print(f'--- [{k}] {u}'); print(b[:LIM].replace('\r','')); print()
