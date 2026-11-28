#!/bin/sh
# Banc de l'ecran d'enrolement (issue #159). Voir README.md.
#
#   ui/test/enrol-flow/run.sh current   # l'interface du depot (ui/dist, construite par tools/build_ui.sh) : doit passer
#   ui/test/enrol-flow/run.sh legacy    # l'interface 0.2.16 (temoin) : doit reproduire le defaut
set -e
cd "$(dirname "$0")"
MODE=${1:-current}
LEGACY_TAG=v0.2.16
PORT=${PORT:-8159}
WORK=$(mktemp -d)
trap 'kill "$BOX" 2>/dev/null; rm -rf "$WORK"' EXIT
cc -std=gnu11 -Wall -Wextra -shared -fPIC -I ../../../firmware/main ../../../firmware/main/enrol_tx.c -o "$WORK/libenrol_tx.so"
if [ "$MODE" = legacy ]; then
  WEB="$WORK/web"; mkdir "$WEB"
  # Le tag n'est pas dans un clone superficiel (CI) : on le prend ici, seul endroit ou il est nomme.
  git rev-parse -q --verify "refs/tags/$LEGACY_TAG" >/dev/null || git fetch -q --depth 1 origin tag "$LEGACY_TAG"
  for f in $(git ls-tree --name-only "$LEGACY_TAG" ../../../firmware/main/web/); do
    git show "$LEGACY_TAG:./$f" > "$WEB/$(basename "$f")"
  done
  EXPECT=fail
else
  WEB="$WORK/web"; mkdir "$WEB"
  cp ../../dist/* "$WEB/"
  cp ../../../firmware/main/web/profiles.json ../../../firmware/main/web/pyramid-detect.js "$WEB/" 2>/dev/null || true
  EXPECT=pass
fi
python3 fakebox.py "$PORT" "$WEB" "$WORK/libenrol_tx.so" &
BOX=$!
sleep 1
# Un port deja pris ferait repondre un AUTRE serveur : le banc mesurerait autre chose sans le dire.
kill -0 "$BOX" 2>/dev/null || { echo "run.sh : le faux boitier n'a pas demarre (port $PORT pris ?)" >&2; exit 1; }
python3 scenarios.py "http://127.0.0.1:$PORT" "$MODE" "$EXPECT"
