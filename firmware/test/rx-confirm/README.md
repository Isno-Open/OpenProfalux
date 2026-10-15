# Banc de test de la confirmation des trames reçues

Teste `rx_confirm.c`, compilé **tel quel**, sur PC. Une trame reçue n'est
retenue que confirmée par une seconde trame identique de la même rafale :
mêmes 64 premiers bits (code tournant, serial, bouton), bits VLOW et Repeat
ignorés, moins de 500 ms d'écart (issue #39). L'unité est **pure** (aucune
dépendance ESP-IDF), donc un simple `gcc`/`clang` suffit.

## Lancer

Depuis ce dossier :

```bash
cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/rx_confirm.c -o rx_confirm_host_test
./rx_confirm_host_test
```

Ou via CMake : `cmake -B build && cmake --build build && ./build/rx_confirm_host_test`.
Le programme rend 0 si tout passe, 1 sinon.

## Ce qui est vérifié

| Test | Scénario | Attendu |
|---|---|---|
| T0 | Deux trames identiques (Repeat 0 puis 1) | La 1re seule n'est pas retenue, la 2e confirme ; trame de 66 bits disponible |
| T1 | Un bit faux dans le serial | Pas de confirmation, ni du fantôme ni par lui |
| T2 | VLOW et Repeat différents | Confirmée (bits 64 et 65 ignorés) |
| T3 | 2e trame après la fenêtre de 500 ms | Nouvel appui, pas une confirmation |
| T4 | Trames coupées (60, 64 bits) | 60 bits ignorée ; 64 bits confirme, mais pas de trame complète sans 66 bits |
| T5 | 1re trame complète, 2e coupée | La trame complète retenue est la 1re |
| T6 | Autre code tournant | La série repart de zéro |
