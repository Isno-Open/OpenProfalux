#!/usr/bin/env bash
# Build des binaires de TEST OpenProfalux, un par carte declaree dans boards/*.json, avec
# une version IDENTIFIABLE dans l'UI pour que le testeur puisse verifier son flash.
#
# Pourquoi : nos builds de test gardaient le numero de release (0.2.11), donc un testeur ne
# pouvait pas distinguer la build de test de l'ancienne release. Ici la version embarquee
# devient "<prochaine release>-<label>", ex. 0.2.12-test9, affichee au boot et dans l'UI.
#
# Usage :
#   ./build-test.sh <label>        # le label doit contenir "test", ex. test9
#
# Produit dans ./release-test/ :
#   openprofalux-<carte>-<label>.bin        app seule, pour l'onglet OTA du boitier / HA
#   openprofalux-<carte>-<label>-full.bin   bootloader + table + app, a flasher a 0x0 (USB)
#
# Difference avec build-release.sh : pas de garde de tag (une build de test n'est pas une
# release), la version vient de l'env OPF_VERSION (voir CMakeLists.txt), et PROJECT_VER
# committe n'est pas touche. Prerequis identiques : ESP-IDF v6.1 (idf.py) ou le conteneur.
set -euo pipefail
cd "$(dirname "$0")"

LABEL="${1:-}"
[ -n "$LABEL" ] || { echo "usage: $0 <label>  (ex. test9) ; le label doit contenir 'test'"; exit 1; }
case "$LABEL" in *test*) : ;; *) echo "le label doit contenir 'test' (recu: $LABEL)"; exit 1 ;; esac

# Prochaine version = PROJECT_VER committe avec son dernier nombre +1 (0.2.11 -> 0.2.12),
# comme les tags de test v0.2.12-testN. Se recale tout seul apres une release.
BASE="$(grep -oE '"[0-9]+\.[0-9]+\.[0-9]+"' CMakeLists.txt | head -1 | tr -d '"')"
[ -n "$BASE" ] || { echo "PROJECT_VER introuvable dans CMakeLists.txt"; exit 1; }
NEXT="$(python3 -c "v='$BASE'.split('.'); v[-1]=str(int(v[-1])+1); print('.'.join(v))")"
FULL="${NEXT}-${LABEL}"
export OPF_VERSION="$FULL"

# Deux facons d'executer, pour la meme sortie : un environnement ESP-IDF v6.1 actif, ou a
# defaut le conteneur officiel (meme version que la CI). OPF_VERSION est passe au conteneur.
if ! command -v idf.py >/dev/null; then
  [ "${ISNO_NO_DOCKER:-0}" = "1" ] && { echo "idf.py absent et docker interdit"; exit 1; }
  command -v docker >/dev/null || { echo "ni idf.py ni docker : active l'environnement ESP-IDF v6.1"; exit 1; }
  echo ">> idf.py absent, on passe par ${IDF_IMAGE:-espressif/idf:v6.1}"
  exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e OPF_VERSION="$FULL" \
    -v "$(cd .. && pwd)":/project -w /project/firmware \
    "${IDF_IMAGE:-espressif/idf:v6.1}" bash build-test.sh "$@"
fi

REL="$(pwd)/release-test"; mkdir -p "$REL"
echo ">> version embarquee : $FULL"
for f in ../boards/*.json; do
  board="$(basename "$f" .json)"
  target="$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['mcu'])" "$f")"
  bdir="build-test-$board"
  echo ">> build $board ($target)"
  rm -rf "$bdir"
  idf.py -B "$bdir" set-target "$target" >/dev/null
  idf.py -B "$bdir" -DBOARD="$board" build >/dev/null
  cp "$bdir/openprofalux.bin" "$REL/openprofalux-$board-$LABEL.bin"
  idf.py -B "$bdir" merge-bin -o "$REL/openprofalux-$board-$LABEL-full.bin" >/dev/null
  echo "   app=$(stat -c%s "$REL/openprofalux-$board-$LABEL.bin") o  full=$(stat -c%s "$REL/openprofalux-$board-$LABEL-full.bin") o"
done

echo "== binaires de test produits (version $FULL) =="; ls -la "$REL"/*.bin | awk '{print $5, $NF}'
echo "Publier en pre-release (exclue de /releases/latest, donc pas d'OTA auto) :"
echo "  gh auth switch --user Shad107"
echo "  gh release create v$FULL --prerelease --title v$FULL --notes 'build de test' $REL/openprofalux-*.bin"
echo "  # ou, si la pre-release existe deja : gh release upload v$FULL $REL/openprofalux-*.bin --clobber"
