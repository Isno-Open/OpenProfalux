# Banc de test du balayage RSSI

Teste `freq_sweep.c`, compilé **tel quel**, sur PC (issue #51). À partir des RSSI
relevés à chaque pas d'un balayage de la fréquence de réception, l'unité trouve la
porteuse de la télécommande : le filtre de réception donne un sommet plat, où le
RSSI max d'une émission OOK fluctue de pas en pas ; on prend donc le milieu des bords
du plateau, au seuil à mi-hauteur (en dB) entre le bruit et le pic, interpolés entre
deux pas.
L'unité est **pure** (aucune dépendance ESP-IDF), un simple `gcc`/`clang` suffit.

## Lancer

```bash
cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/freq_sweep.c -o freq_sweep_host_test
./freq_sweep_host_test
```

Ou via CMake : `cmake -B build && cmake --build build && ./build/freq_sweep_host_test`.

## Ce qui est vérifié

Courbes synthétiques : réponse d'un filtre de ~58 kHz (sommet plat sur ±20 kHz,
flancs jusqu'au bruit à ±45 kHz), balayage ±150 kHz par pas de 10 kHz.

| Test | Scénario | Attendu |
|---|---|---|
| T0 | Porteuse 12 kHz sous le profil, sans bruit | Trouvée à ±2 kHz |
| T1 | Même chose avec ±1,5 dB de bruit | Trouvée à ±4 kHz |
| T2 | Porteuse décalée de +73 kHz | Trouvée à ±4 kHz |
| T3 | Signal à 6 dB du bruit | Refusé (`FSWEEP_WEAK`) |
| T4 | Porteuse hors du balayage | Refusé (`FSWEEP_AT_EDGE`) |
| T6, T7 | Deux balayages réels successifs (d1-mini, télécommande Eveno sur P), plateau qui fluctue de 12 dB | Même porteuse à ±3 kHz |
| T5 | Moins de 5 pas | Refusé (`FSWEEP_TOO_FEW`) |
