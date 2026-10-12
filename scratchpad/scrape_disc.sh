#!/bin/bash
CURSOR=null
PAGE=0
while : ; do
  PAGE=$((PAGE+1))
  if [ "$CURSOR" = "null" ]; then AFTER="null"; else AFTER="\"$CURSOR\""; fi
  QUERY="{ repository(owner:\"boykopovar\", name:\"AnyPS5\"){ discussions(first:20, after:$AFTER){ pageInfo{ hasNextPage endCursor } nodes{ number title body url createdAt category{name} comments(first:100){ totalCount pageInfo{hasNextPage} nodes{ id body url replies(first:100){ totalCount pageInfo{hasNextPage} nodes{ id body url } } } } } } } }"
  gh api graphql -f query="$QUERY" > page_$PAGE.json
  HAS=$(python -c "import json;d=json.load(open('page_$PAGE.json',encoding='utf-8'));print(d['data']['repository']['discussions']['pageInfo']['hasNextPage'])")
  CURSOR=$(python -c "import json;d=json.load(open('page_$PAGE.json',encoding='utf-8'));print(d['data']['repository']['discussions']['pageInfo']['endCursor'])")
  echo "page $PAGE hasNext=$HAS"
  [ "$HAS" = "True" ] || break
done
