#!/bin/bash
# Produit les binaires d'une release, une paire par carte declaree, dans le
# conteneur ESP-IDF officiel. Reproductible : rien ne depend du poste.
#
#   bash scripts/release.sh                 # toutes les cartes de boards/
#   bash scripts/release.sh m5-atom-lite    # une seule
#
# Les cartes ne sont pas listees ici : elles viennent de boards/<carte>.json,
# seul endroit ou une carte se declare, comme pour la CI et la compilation.
# En ajouter une, c'est ajouter un fichier.
#
# Pour chaque carte :
#   openprofalux-<carte>-ota.bin   l'application seule, pour l'onglet OTA et
#                                  l'entite update de Home Assistant
#   openprofalux-<carte>-full.bin  bootloader + table de partitions + ota_data
#                                  + application, fusionnes a 0x0, pour une
#                                  carte neuve (esptool write_flash 0x0)
#
# La version vient de PROJECT_VER dans firmware/CMakeLists.txt, seule declaration,
# et chaque binaire est verifie contre elle avant d'etre copie.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Meme image que la CI : une release ne doit pas etre batie avec un autre
# compilateur que celui qui a verifie le code.
IMAGE="${IDF_IMAGE:-espressif/idf:v6.1}"

cd "$ROOT/firmware"
VER=$(sed -n 's/^set(PROJECT_VER "\(.*\)")/\1/p' CMakeLists.txt)
[ -n "$VER" ] || { echo "PROJECT_VER introuvable dans firmware/CMakeLists.txt"; exit 1; }

if [ $# -gt 0 ]; then
  CARTES="$*"
else
  CARTES=$(ls "$ROOT/boards"/*.json | xargs -n1 basename | sed 's/\.json$//')
fi

mkdir -p "$ROOT/dist"
# -u : rien n'est ecrit en root dans l'arborescence. HOME=/tmp car l'utilisateur
# n'a pas de foyer dans l'image.
run() { docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
          -v "$ROOT":/project -w /project/firmware "$IMAGE" bash -c "$*"; }

for carte in $CARTES; do
  json="$ROOT/boards/$carte.json"
  [ -f "$json" ] || { echo "carte inconnue : $carte (voir boards/)"; exit 1; }
  cible=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['mcu'])" "$json")
  B="build-release-$carte"

  echo "== $carte ($cible, $VER, $IMAGE)"
  # Un repertoire par carte : deux cibles ne partagent pas un cache CMake.
  #
  # On repart d'un repertoire neuf. Un build interrompu laisse un repertoire que
  # `set-target` refuse de nettoyer ("doesn't seem to be a CMake build
  # directory"), et la carte est alors sautee. Une release se fabrique de toute
  # facon depuis zero : c'est ce qui la rend reproductible.
  rm -rf "$B"
  run "idf.py -B $B set-target $cible && idf.py -B $B -DBOARD=$carte build" | tail -n 3

  compilee=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['project_version'])" "$B/project_description.json")
  [ "$compilee" = "$VER" ] || { echo "version compilee '$compilee' != '$VER' pour $carte"; exit 1; }

  cp "$B/openprofalux.bin" "$ROOT/dist/openprofalux-$carte-ota.bin"
  run "cd $B && esptool.py --chip $cible merge_bin -o full.bin @flash_args" >/dev/null
  cp "$B/full.bin" "$ROOT/dist/openprofalux-$carte-full.bin"
done

echo
ls -la "$ROOT/dist"/*.bin
