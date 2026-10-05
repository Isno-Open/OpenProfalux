# Banc de test du décodeur KeeLoq

Teste `keeloq_decode.c` (décodage OOK HCS30x/KeeLoq : de-glitch des fronts,
ancrage sur l'en-tête, vote majoritaire), compilé **tel quel**, sur PC. Le
décodeur est **pur** (aucune dépendance ESP-IDF), donc un simple `gcc`/`clang`
suffit : pas d'IDF, pas de matériel, quelques millisecondes.

## Lancer

Depuis ce dossier :

```bash
cc -std=gnu11 -Wall -Wextra -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/keeloq_decode.c -o keeloq_host_test
./keeloq_host_test
```

Ou via CMake : `cmake -B build && cmake --build build && ./build/keeloq_host_test`.
Le programme rend 0 si tout passe, 1 sinon.

## Ce qui est vérifié

Le banc fabrique des trames « en bouchon » avec les temps d'une HCS301 réelle
(Te ≈ 430 µs, `'0'` = 2Te haut + 1Te bas, `'1'` = 1Te haut + 2Te bas, en-tête
LOW long ≈ 10 Te), les passe au décodeur, et compare la chaîne de bits rendue à
celle attendue.

| Test | Scénario | Attendu |
|---|---|---|
| T0 | Trame propre, une répétition | 66 bits exacts, Te ≈ 430 |
| T1 | 66e bit coupé par l'idle (pas de BAS final) | 66 bits quand même (correctif @Akkeoss, issue #12) |
| T2 | Un glitch de démod (sliver < `GLITCH_US`) fend un HAUT | Le de-glitch le jette, recolle le HAUT, trame intacte |
| T3 | Du bruit sans en-tête long | Aucun ancrage → `-4` |
| T4 | Trois répétitions dont une avec un bit faux | Le vote 2 contre 1 corrige le bit |

Le banc est compilé avec AddressSanitizer : toute lecture/écriture hors des
tampons du décodeur (liste de fronts, mots du vote) l'arrête net.

## Ce qu'il n'exerce pas

La capture elle-même (RMT, CC1101) et le reste de `cc1101.c` : seul le décodeur
pur est couvert. Les symboles d'entrée sont fabriqués, pas captés sur l'air.
