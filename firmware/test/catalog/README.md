# Banc de test du catalogue

Teste `catalog.c`, compilé **tel quel**, sur le **vrai** `firmware/main/web/profiles.json` (cible `linux`
d'ESP-IDF, pour le cJSON du firmware). Aucun matériel requis.

## Lancer

Depuis la racine du dépôt, avec l'image ESP-IDF officielle :

```bash
docker run --rm -v "$PWD/firmware:/project/firmware" \
  -w /project/firmware/test/catalog espressif/idf:v6.1 \
  bash -c "idf.py --preview set-target linux && idf.py build && ./build/catalog_host_test.elf ../../main/web/profiles.json"
```

Le programme renvoie 0 si tout passe, 1 sinon.

## Ce qui est vérifié

- **Aucune émission ne change** : le firmware lit dans le catalogue exactement les valeurs qu'il avait en dur
  jusqu'à la v0.2.17 (boutons KeeLoq, trames par appui, salves d'apprentissage, rejeu, Somfy).
- **Les codes Somfy du catalogue sont ceux de la norme** que l'encodeur connaît (`somfy_rts.h`).
- **L'ordre des modèles ne bouge pas** : une identité d'enrôlement garde en NVS le numéro de son modèle.
- **Un catalogue faux est refusé en entier** : JSON illisible, encodeur inconnu, deux protocoles pour un
  encodeur, code hors 0..15, trames par appui absentes ou non entières, enrôlement incomplet, trop de
  modèles. Rien n'est lu, donc rien n'est émis, et un bon catalogue se relit ensuite.
