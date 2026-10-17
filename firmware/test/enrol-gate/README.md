# Banc de test de la porte d'apprentissage (enrôlement)

Teste `enrol_gate.c`, compilé **tel quel**, sur PC (issue #53). L'émission de la trame
d'apprentissage n'est permise qu'après une capture **réussie** dans la tentative en cours :
une capture ratée ne laisse plus l'identité de la capture précédente utilisable. L'unité
est **pure** (aucune dépendance ESP-IDF), un simple `gcc`/`clang` suffit.

## Lancer

```bash
cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/enrol_gate.c -o enrol_gate_host_test
./enrol_gate_host_test
```

## Ce qui est vérifié

| Test | Scénario | Attendu |
|---|---|---|
| T0 | Démarrage (identité restée en NVS) | Apprentissage refusé |
| T1 | Capture réussie | Refusé pendant la capture, permis ensuite |
| T2 | Capture réussie puis nouvelle capture **ratée** (le cas de #53) | Refusé, message d'échec |
| T3 | Trame reçue mais identité refusée (table pleine) | Refusé |
| T4 | Oubli ou volet enregistré après une réussite | Refusé |
| T5 | Résultat arrivé hors capture (périmé) | Ignoré |
| T6 | Nouvelle capture lancée après une réussite | Refusé jusqu'au résultat |
