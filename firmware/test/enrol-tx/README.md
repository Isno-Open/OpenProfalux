# Banc de test des réglages d'émission de l'enrôlement

Teste `enrol_tx.c`, compilé **tel quel**, sur PC (issue #159). Le panneau « Réglages avancés
d'émission » n'était lu qu'à la capture : une porteuse ou un TE changés ensuite n'étaient jamais
émis, alors que l'écran les montrait. La capture ouvre maintenant la session avec ses réglages,
l'apprentissage les remplace s'il en apporte, et la fin de session revient au profil. L'unité est
**pure** (aucune dépendance ESP-IDF), un simple `gcc`/`clang` suffit.

## Lancer

```bash
cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address -g \
   -I main -I ../../main main/test_main.c ../../main/enrol_tx.c -o enrol_tx_host_test
./enrol_tx_host_test
```

## Ce qui est vérifié

| Test | Scénario | Attendu |
|---|---|---|
| T0 | Pas de session | Tout hérite du profil |
| T1 | Capture avec les réglages du panneau | Porteuse, TE et options de la session |
| T2 | Réglages saisis **après** la capture (le cas de #159) | Émis à l'apprentissage |
| T3 | Apprentissage sans réglages (ancien client) | Ceux de la capture restent |
| T4 | Apprentissage revenu aux valeurs du profil | La session hérite |
| T5 | Identité désélectionnée ou enregistrée | Tout hérite |
| T6 | TE affiché au journal | Celui de la session, sinon celui de la radio |

Le banc détecte chaque règle cassée exprès : apprentissage qui ignore ses réglages (T2),
apprentissage sans réglages qui écrase la session (T3), fin de session qui garde la porteuse (T5),
journal qui relit la radio (T6).

## Ce que le banc ne couvre pas

Les appels dans `web_ui.c` (`h_pfx_capture`, `h_pfx_learn`, `h_pfx_forget`, `h_pfx_save_volet`) et
dans `pfx_enrol.c` : ces fichiers dépendent d'ESP-IDF et n'ont pas de banc sur PC. Retirer l'appel
à `enrol_tx_learn` dans `h_pfx_learn` ne fait échouer aucun test.
