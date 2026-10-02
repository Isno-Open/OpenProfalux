# Banc de test de la persistance de la config

Teste `cfg_model.c` (sérialisation JSON des volets) et `cfg_store.c` (rangement en NVS),
compilés **tels quels**, sur la **vraie bibliothèque NVS d'ESP-IDF** avec une partition
émulée sur PC (cible `linux`). Aucun matériel requis, une seconde d'exécution.

## Lancer

Depuis la racine du dépôt, avec l'image ESP-IDF officielle (celle de `Dockerfile.esp-idf`) :

```bash
docker run --rm -v "$PWD/firmware:/project/firmware" \
  -w /project/firmware/test/cfg_store espressif/idf:v5.2.2 \
  bash -c "idf.py --preview set-target linux && idf.py build && ./build/cfg_store_host_test.elf"
```

Ou, avec un ESP-IDF 5.2 installé sous Linux, les trois mêmes commandes depuis ce
dossier. Le programme renvoie 0 si tout passe, 1 sinon.

Dans le conteneur `docker compose` du projet, `IDF_TARGET=esp32` est défini et empêche
la compilation pour PC : faire `unset IDF_TARGET` avant.

## Ce qui est vérifié

Le banc manipule de vrais volets (trames de 66 bits, centrale, télécommandes nommées),
sérialisés et relus par le code du firmware. L'état de départ reproduit l'occupation
d'une NVS de 16 Ko relevée sur un boîtier en usage réel : pile Wi-Fi, calibration radio,
réglages, 6 volets, 15 télécommandes, dataset de trames. Les contenus sont factices,
les tailles voisines de celles mesurées.

| Test | Scénario | Attendu |
|---|---|---|
| T0 | Export → import → export ; ancien export sans rang d'affichage ; volet en double | Config identique champ par champ ; `order` absent → -1 ; doublon ignoré |
| T1 | Ancien format (une chaîne unique), dataset qui grossit | Le bug d'origine est reproduit : les sauvegardes échouent |
| T2 | Premier démarrage du nouveau firmware sur cette NVS | Migration réussie, chaque champ identique, anciennes clés effacées |
| T3 | 10 000 mouvements de volets | Aucun échec |
| T4 | Volets ajoutés jusqu'à saturation | Un refus laisse `nv` cohérent ; un ajout refusé n'est pas annoncé |
| T5 | Coupure de courant à **chaque** point d'écriture de la migration | Config toujours lisible et intacte ; migration terminée au démarrage suivant |
| T6 | Coupure à chaque point d'une sauvegarde ordinaire | Chaque volet a son ancienne ou sa nouvelle valeur, jamais autre chose |
| T7 | Coupure à chaque point de la suppression d'un volet | Aucun autre volet perdu ni altéré |
| T8 | Boîtier neuf ; document impossible à produire (mémoire) | `nv` n'annonce rien de plus |
| T9 | Retour à un ancien firmware qui modifie la config, puis mise à jour, pour 7 tailles de config, puis avec une coupure à chaque point | La config de l'ancien firmware fait foi, pas les clés par volet périmées |
| T10 | Même retour, mais l'ancien firmware n'a pu écrire qu'une config vide | Les volets des clés par volet sont conservés |
| T11 | Le fichier du dataset ne peut pas être écrit | Config intacte, dataset gardé en NVS ; tout se termine quand le stockage revient |

## Comment la coupure est simulée

La NVS écrit en flash par `esp_partition_write_raw` et `esp_partition_erase_range`. Le
banc les intercepte à l'édition de liens (`ld --wrap`) : au point choisi, l'écriture en
cours est tronquée, puis plus rien n'atteint la flash. Le « redémarrage » ferme et
rouvre la NVS, qui relit tout depuis la flash.

## Ce que le banc exerce, et ce qu'il n'exerce pas

Le démarrage joué par le banc (`boot_sequence`) fait les mêmes appels que
`shutters_init()`, dans le même ordre : `cfg_model_load`, `cfg_frames_load`,
`cfg_store_finish_boot`. L'ordre des opérations de migration et la règle « n'effacer
l'ancien qu'après avoir écrit le nouveau » sont dans `cfg_store_finish_boot`, donc
testés tels quels. Comme le firmware, le banc réécrit à la migration **ce qui a été lu
en flash**.

Le banc détecte bien un code faux. Vérifié en cassant le firmware de neuf façons, toutes
détectées :

| Mutation | Détectée par |
|---|---|
| `nv` écrit même après une erreur | T4, T8 |
| Ancienne clé effacée avant l'écriture de la nouvelle config | T5, T9 |
| Dataset effacé après la migration au lieu d'avant | T2 |
| Clés par volet toujours prioritaires sur l'ancienne clé | T9 |
| Ancienne clé toujours prioritaire | T10 |
| Sérialisation : un champ n'est plus écrit | T0, T4, T8 |
| Lecture : un champ lu au mauvais endroit | T0, T4, T8, T9 |
| Dataset déclaré rangé alors que le fichier n'a pas pu être écrit | T11 |
| Volet en double non ignoré à la lecture | T0, T7 |

Non couvert :

- Le comportement de SPIFFS lui-même : le fichier du dataset est écrit sur le disque du
  PC. Seul le cas « écriture impossible » est joué, pas une coupure pendant l'écriture
  du fichier.
- Le reste de `shutters.c` : apprentissage, pilotage, suivi de position.

## Autre taille de NVS

Le banc s'adapte à la taille déclarée dans `partitions.csv` (32 Ko au plus). Pour
essayer 32 Ko : `nvs` à `0x8000`, et décaler `phy_init` (`0x11000`) et `factory`
(`0x20000`).
