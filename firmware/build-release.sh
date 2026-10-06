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

# Version : PROJECT_VER dans CMakeLists.txt, seule declaration. Une version deja
# publiee ne se refabrique pas : v0.2.6 a ete taguee avec PROJECT_VER encore a
# 0.2.5, et ses binaires s'annoncaient 0.2.5 (la mise a jour leur etait proposee
# en boucle). Le tag vX.Y.Z deja pose sur un AUTRE commit veut dire que
# PROJECT_VER n'a pas ete monte depuis.
VER="$(sed -n 's/^set(PROJECT_VER "\(.*\)")/\1/p' CMakeLists.txt)"
[ -n "$VER" ] || { echo "PROJECT_VER introuvable dans CMakeLists.txt"; exit 1; }
if tag="$(git rev-parse -q --verify "refs/tags/v$VER^{commit}")"; then
  [ "$tag" = "$(git rev-parse HEAD)" ] || {
    echo "v$VER est deja publiee (tag sur ${tag:0:7}) : monter PROJECT_VER dans CMakeLists.txt"; exit 1; }
fi

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

# Les boitiers en v0.2.3 (avant boards/<carte>.json) cherchent leur mise a jour
# sous les anciens noms, openprofalux-atom-ota.bin et openprofalux-devkit-ota.bin,
# dans la DERNIERE release. Memes cartes (brochage et table de partitions
# identiques) : une copie sous ces noms leur permet de se mettre a jour depuis
# GitHub. Pas de copie du -full.bin, qui ne sert qu'au flash USB.
for paire in m5-atom-lite:atom external:devkit; do
  src="$REL/openprofalux-${paire%%:*}-ota.bin"
  if [ -f "$src" ]; then cp "$src" "$REL/openprofalux-${paire##*:}-ota.bin"; fi
done

echo "== binaires produits =="; ls -la "$REL"/*.bin | awk '{print $5, $NF}'
echo "Faits depuis $(git rev-parse --short HEAD)$(git diff --quiet HEAD || echo ' + modifications NON commitees') : c'est ce commit qu'il faut taguer v$VER."
echo "Upload : gh auth switch --user Shad107 puis"
echo "  gh release upload v$VER $REL/openprofalux-*.bin --clobber"
