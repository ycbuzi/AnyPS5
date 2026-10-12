#!/bin/bash
CURSOR=null
PAGE=0
while : ; do
  PAGE=$((PAGE+1))
  if [ "$CURSOR" = "null" ]; then AFTER="null"; else AFTER="\"$CURSOR\""; fi
  QUERY="{ repository(owner:\"boykopovar\", name:\"AnyPS5\"){ issues(first:25, after:$AFTER, orderBy:{field:CREATED_AT, direction:ASC}){ pageInfo{ hasNextPage endCursor } nodes{ number title body url state comments(first:100){ totalCount pageInfo{hasNextPage} nodes{ body url } } } } } }"
  gh api graphql -f query="$QUERY" > iss_$PAGE.json
  HAS=$(python -c "import json;d=json.load(open('iss_$PAGE.json',encoding='utf-8'));print(d['data']['repository']['issues']['pageInfo']['hasNextPage'])")
  CURSOR=$(python -c "import json;d=json.load(open('iss_$PAGE.json',encoding='utf-8'));print(d['data']['repository']['issues']['pageInfo']['endCursor'])")
  echo "iss page $PAGE hasNext=$HAS"
  [ "$HAS" = "True" ] || break
done
