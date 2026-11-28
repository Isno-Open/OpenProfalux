#!/bin/sh
# Construit l'interface web du boitier (ui/, Vue 3 + Vite) dans ui/dist : index.html, app.js et style.css,
# que firmware/main/CMakeLists.txt compresse et embarque. A lancer avant idf.py, et apres chaque
# modification de ui/ ou de firmware/main/web/ (profiles.json, profiles.schema.json, pyramid-detect.js).
#
#   tools/build_ui.sh
#
# Prend npm s'il est installe (Node 22.18 ou plus recent : le generateur du catalogue est en TypeScript, lance tel quel), sinon Docker avec l'image figee ci-dessous :
# rien a installer sur le poste. Les versions des paquets sont figees par ui/package-lock.json (npm ci).
set -e
NODE_IMAGE=node:22.23.3-alpine
cd "$(dirname "$0")/.."
if command -v npm >/dev/null 2>&1; then
  (cd ui && npm ci --no-audit --no-fund && npm run build)
elif command -v docker >/dev/null 2>&1; then
  # Tout le depot est monte : l'interface lit profiles.json et pyramid-detect.js dans firmware/main/web.
  docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/repo -w /repo/ui "$NODE_IMAGE" \
    sh -c "npm ci --no-audit --no-fund && npm run build"
else
  echo "build_ui.sh : ni npm ni Docker. Installe Node 22 (https://nodejs.org) ou Docker, puis relance." >&2
  exit 1
fi
echo "Interface construite dans ui/dist :"
ls -l ui/dist
