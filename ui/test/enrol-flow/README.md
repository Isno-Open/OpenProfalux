# Banc de l'écran d'enrôlement

Rejoue dans un vrai navigateur les gestes du rapport #159, sans boîtier : l'utilisateur règle la porteuse
et le TE dans « Réglages avancés d'émission », capture, puis émet la trame d'apprentissage. Le banc compare
ce qu'il a **saisi** à ce que le panneau **montre** après chaque geste et à ce que le boîtier **émet**.

En 0.2.16, le panneau revenait aux valeurs du profil après chaque capture et à chaque retour sur l'écran.
Une première capture ratée (télécommande trop loin) suivie d'une recapture, ou un aller-retour sur un autre
onglet, faisaient donc partir l'apprentissage sur 868 425 kHz et 400 µs au lieu des valeurs saisies. Le
moteur n'entendait rien.

## Ce qui tourne

- `fakebox.py` : un faux boîtier. Il sert l'interface, répond aux lectures avec les réponses de synthèse de
  `fixtures/`, et tient la session d'enrôlement avec **`firmware/main/enrol_tx.c` lui-même**, compilé en
  bibliothèque. La règle n'est pas recopiée en Python.
- `scenarios.py` : les gestes, pilotés par Playwright (Chromium sans tête).
- `run.sh` : compile la bibliothèque, démarre le faux boîtier, lance les scénarios.

## Lancer

```bash
pip install -r ui/test/enrol-flow/requirements.txt && playwright install chromium
tools/build_ui.sh                        # ui/dist
ui/test/enrol-flow/run.sh current        # l'interface du dépôt : tout doit passer
ui/test/enrol-flow/run.sh legacy         # l'interface 0.2.16 : le défaut doit être reproduit
```

## Scénarios

| | Gestes | 0.2.16 |
|---|---|---|
| A | saisir, capturer, émettre | le panneau perd la saisie |
| D | saisir, capture ratée, recapturer, émettre | émission aux valeurs du profil |
| E | saisir, capture ratée, ressaisir, recapturer, émettre | le panneau perd la saisie |
| F | saisir, autre onglet et retour, capturer, émettre | émission aux valeurs du profil |
| G | saisir, capturer, autre onglet et retour, émettre | le panneau perd la saisie |
| H | capturer, saisir, émettre | émission aux valeurs du profil |

## Le témoin

Un banc qui ne peut pas échouer ne prouve rien. `run.sh legacy` rejoue les mêmes gestes sur l'interface du
tag `v0.2.16`, lue dans l'historique git, et **exige** que A, D, F et H échouent. La CI lance les deux.

Mutations essayées sur l'interface du dépôt, chacune fait échouer le banc :

| Mutation | Scénarios qui tombent |
|---|---|
| le panneau n'affiche plus la session du boîtier | F, G |
| la saisie n'est plus gardée au changement d'onglet | F |
| l'apprentissage n'envoie plus les réglages | H |

## Ce que le banc ne couvre pas

- Les gestionnaires de `web_ui.c` (lecture du JSON, appels à `enrol_tx`) : le faux boîtier les refait en
  Python autour de la vraie règle. Un appel retiré dans `web_ui.c` ne fait échouer aucun test.
- La radio : l'émission est journalisée, pas émise.
