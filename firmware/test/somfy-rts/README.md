# Banc de test du codec Somfy RTS

Teste `somfy_rts.c` (le niveau **logique** de la trame Somfy RTS : somme de
contrôle, obscurcissement en cascade, aller-retour des champs, dépliage
Manchester), compilé **tel quel**, sur PC. Le module est **pur** (aucune
dépendance ESP-IDF) : un simple `gcc`/`clang` suffit.

## Lancer

Depuis ce dossier :

```bash
cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/somfy_rts.c -o somfy_rts_host_test
./somfy_rts_host_test
```

Ou via CMake : `cmake -B build && cmake --build build && ./build/somfy_rts_host_test`.
Le programme rend 0 si tout passe, 1 sinon.

## Ce qui est vérifié

| Test | Scénario | Attendu |
|---|---|---|
| T0 | Somme de contrôle sur deux trames en dur | `A7 20 ..` donne `0xF`, trame nulle donne `0x0` |
| T1 | `pack` d'un vecteur connu (clé A7, UP, code 0, adresse 0) | Octets obscurcis `A7 88 88 88 88 88 88` |
| T2 | Aller-retour `pack` puis `unpack` | Clé, commande, code tournant et adresse préservés |
| T3 | Un quartet corrompu après `pack` | `unpack` refuse (somme de contrôle fausse) |
| T4 | Dépliage Manchester d'une trame | 112 symboles, chaque paire est une transition `01`/`10`, recomposition exacte |
| T5 | Tampon Manchester trop court | Rend `-1`, rien n'est écrit |
| T6 | Les 9 commandes RTS | Chaque commande survit à l'aller-retour |
| T7 | Vecteurs réels d'open-rts (implémentation tierce) | `unpack` et `pack` concordent bit à bit sur deux trames réelles |
| T8 | Séquence d'impulsions d'émission (structure) | Sync matériel, sync logiciel à 4550, niveaux adjacents différents, durée totale = somme attendue |
| T9 | Télécommande virtuelle (adresse + code roulant) | Reproduit bit à bit les trames open-rts (clé = quartet bas du code roulant), le code roulant avance et se replie |

## Ce qui N'EST PAS testé ici (attend le banc, issue #73)

La **structure** de la séquence d'émission (ordre des éléments, durées des
constantes, fusion des niveaux) est testée sur PC (T8). En revanche la **polarité
réelle à l'antenne** et la jonction sync logiciel / premier bit ne sont pas
prouvables sans un vrai moteur Somfy : elles seront validées sur une capture réelle
avant toute prétention d'interop.
