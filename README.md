# questbuild-runner

Builds the Linux x86-64 libraries and relinker for the Quest test lane, so the Quest never waits on our own machines.

How it works:
1. Add a line to queue.txt and push it to this branch.
2. The "Quest builds" workflow builds every line that has no `quest-<folder>` release yet.
3. Each build is published as a prerelease. The Quest downloads it from the public release URL, which needs no token.

These are local test builds. Nothing here is posted upstream.
