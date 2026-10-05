#!/usr/bin/env bash
# Build des binaires de release OpenProfalux pour CHAQUE carte declaree dans
# boards/*.json, depuis le commit courant. Produit dans ./release/ :
#   openprofalux-<carte>-ota.bin   app seule, pour l'onglet OTA du boitier / HA
#   openprofalux-<carte>-full.bin  bootloader + table + app, a flasher a 0x0
#
# Prerequis : un environnement ESP-IDF v6.1 actif (idf.py sur le PATH), la meme
# version que la CI (espressif/idf:v6.1). Les cartes et leur cible viennent de
# boards/*.json, seul endroit ou une carte se declare : ajouter une carte, c'est
# ajouter un fichier, rien a tenir a jour ici.
#
# PIEGES EVITES :
#  - un repertoire de build PAR carte : deux cibles ne partagent pas le meme
#    cache CMake, et un sdkconfig partage ferait sortir une variante pour une
#    autre (le bug historique ATOM/DevKit). On passe -DBOARD comme la CI.
#  - les offsets du merge ne sont PAS codes en dur : `idf.py merge-bin` les lit
#    dans flasher_args.json, donc le bootloader tombe a 0x0 sur ESP32-S3 et a
#    0x1000 sur ESP32 classique, et la taille de flash suit la carte.
set -euo pipefail
cd "$(dirname "$0")"
command -v idf.py >/dev/null || { echo "idf.py absent : active l'environnement ESP-IDF v6.1"; exit 1; }
REL="$(pwd)/release"; mkdir -p "$REL"

# Les cartes vivent a la racine du depot (boards/), le firmware est un sous-dossier.
for f in ../boards/*.json; do
  board="$(basename "$f" .json)"
  target="$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['mcu'])" "$f")"
  bdir="build-rel-$board"
  echo ">> build $board ($target)"
  rm -rf "$bdir"
  idf.py -B "$bdir" set-target "$target" >/dev/null
  idf.py -B "$bdir" -DBOARD="$board" build >/dev/null
  cp "$bdir/openprofalux.bin" "$REL/openprofalux-$board-ota.bin"
  idf.py -B "$bdir" merge-bin -o "$REL/openprofalux-$board-full.bin" >/dev/null
  echo "   ota=$(stat -c%s "$REL/openprofalux-$board-ota.bin") o  full=$(stat -c%s "$REL/openprofalux-$board-full.bin") o"
done

echo "== binaires produits =="; ls -la "$REL"/*.bin | awk '{print $5, $NF}'
echo "Upload : gh auth switch --user Shad107 puis"
echo "  gh release upload vX.Y.Z $REL/openprofalux-*.bin --clobber"
