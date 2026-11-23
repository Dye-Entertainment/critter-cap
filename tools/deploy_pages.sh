#!/bin/bash
# Publishes site/ (made by `make pages`) to the gh-pages branch of origin.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -f site/index.html ]; then
    echo "site/ is missing; run 'make pages' first" >&2
    exit 1
fi

REMOTE=$(git remote get-url origin)
REV=$(git rev-parse --short HEAD)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cp -r site/. "$TMP/"
cd "$TMP"
git init -q -b gh-pages
git add -A
git -c user.name="$(git -C "$OLDPWD" config user.name)" \
    -c user.email="$(git -C "$OLDPWD" config user.email)" \
    commit -q -m "Deploy critter-cap $REV"
git push -q -f "$REMOTE" gh-pages
echo "Deployed $REV to gh-pages"
