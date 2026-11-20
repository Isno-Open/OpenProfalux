# Interface web du boîtier (Vue 3)

L'interface que sert le boîtier : rendue dans le navigateur, sur l'API REST de `firmware/main/web_ui.c`.
La construction donne trois fichiers aux noms fixes, `index.html`, `app.js` et `style.css`, dans `ui/dist` :
`firmware/main/CMakeLists.txt` les compresse et les embarque, `web_ui.c` les sert sous ces mêmes noms.

## Construire

Depuis la racine du dépôt :

```bash
tools/build_ui.sh
```

`npm` s'il est installé (Node 22.12 ou plus récent), sinon Docker (`node:22.23.3-alpine`). Les versions des
paquets sont figées par `package-lock.json` (`npm ci`). Rien de construit n'est commis. À refaire après chaque
modification de `ui/` ou de `firmware/main/web/` (`profiles.json`, `pyramid-detect.js`), avant `idf.py`.

`npm run build` vérifie d'abord les types (`npm run typecheck`, soit `vue-tsc --noEmit`) : une erreur de type
fait échouer la construction, en local comme en CI.

## Travailler sur l'interface

```bash
cd ui && npm ci

# contre un vrai boîtier : l'API est relayée vers lui, le code se recharge à chaque enregistrement
BOX=192.168.1.50 npm run dev

# sans carte, sur de fausses données (les mêmes champs JSON que le firmware)
npm run dev:mock

# les types seuls, sans construire
npm run typecheck
```

Puis ouvrir l'adresse que donne Vite (http://localhost:5173). Avec les fausses données, `?connexion` dans
l'adresse montre l'écran de connexion (n'importe quel mot de passe sauf « faux »). `npm run build:mock`
construit cette version dans `ui/dist-mock`, servable par n'importe quel serveur statique. Le choix entre
fausses données et vraies routes est fixé à la construction (`__MOCK_DATA__`, voir `vite.config.ts`) : les
fausses données ne sont jamais dans la vraie interface.

## Organisation

- `src/styles/tokens.css` : la charte. Couleurs, rayons et espacements, en sombre et en clair. Les composants
  ne lisent que ces variables.
- `src/components/base/` : les composants génériques (`OpButton`, `OpCard`, `OpField`, `OpSelect`, `OpSwitch`,
  `OpTabs`, `OpSubtabs`, `OpSteps`, `OpDetails`, `OpChip`, `OpTagBox`, `OpTable`, `OpFileInput`, `OpBadge`,
  `OpStatline`, `OpProgress`, `OpToast`, `OpIcon`).
- `src/components/domain/` : les composants d'OpenProfalux, bâtis avec les génériques. Volets : `ShutterTile`,
  `ShutterControl`, `GateControl`, `PositionBar`. Appareils : `AddDevice`, `DetectProposal`, `ProfileChoice`,
  `CaptureSlots`, `TxParamsPanel`, `ChipPicker`, `DeviceRow`, `DeviceEdit`. Système : `RadioList`, `RadioCard`,
  `FreqProfile`, `SweepControl`, `RxGain`, `HealthPanel`, `LiveFramesTable`, `LogView`, `CapturePanel`. Communs :
  `AppHeader`, `AppLogo`, `LoginScreen`, `BatteryBadge`, `SignalBars`.
- `src/views/` : les écrans, qui assemblent ces composants (`devices/`, `network/`, `system/` pour les sections).
- `src/store.ts` : la session, le statut (relu toutes les 3 s), la radio occupée, les messages.
- `src/api/` : la seule porte vers le boîtier. `real.ts` et `real-system.ts` appellent les routes de `web_ui.c`,
  avec les mêmes corps que l'interface d'avant ; `mock.ts` et `mock-system.ts` répondent avec de fausses données
  (développement sans carte). `types.ts` décrit chaque réponse du firmware, avec ses noms de champs JSON : les
  deux couches s'y tiennent, et une fausse donnée qui s'en écarte fait échouer la vérification des types.
- `src/domain.ts` : les valeurs du domaine (commandes, types d'appareil, mécanismes, bandes, états OTA, onglets et
  sections...), déclarées une fois en `as const`. Le code les nomme, il ne compare pas de texte à la main.
- `src/profiles.ts` : tout ce qui se lit dans `firmware/main/web/profiles.json` (types, marques, méthodes, champs
  d'émission, rôles, bornes). Aucun écran ne code en dur un type ou une marque. La table est lue au boîtier après
  la connexion (route protégée) ; les fausses données l'importent depuis le même fichier.
- `src/lib/detection.ts` : la détection de profil, `firmware/main/web/pyramid-detect.js`, qui reste en JavaScript
  à côté de son banc node ; `src/lib/pyramid-detect.d.ts` en déclare les types.
- `src/router.ts` : les adresses d'avant restent valides (`#control`, `#remotes/add`, `#sys/log`...). Wi-Fi, MQTT
  et Cloud sont les sections de Réseau ; `#wifi`, `#mqtt` et `#cloud` y mènent (portail captif : `/#wifi`).
- `src/assets/` et `src/icons.ts` : les icônes, dessinées pour le projet, et leurs noms.

## Règles

- TypeScript en mode `strict` (`tsconfig.json`, avec `noUncheckedIndexedAccess`) : `lang="ts"` dans chaque
  composant, `satisfies` plutôt que `as`.
- Identifiants, fichiers et classes CSS en anglais ; textes affichés et commentaires en français. Les champs JSON
  du firmware (`volets`, `marque`...) et les ancres d'adresse (`#reseau/wifi`) restent tels quels : ce sont des
  données.
- Aucune boîte du navigateur (`alert`, `confirm`, `prompt`) : on confirme dans la page. `tools/check_web_dialogs.py`
  le vérifie en CI.
- Le statut est relu toutes les 3 s : un champ de formulaire copie sa valeur une fois, il ne se lie pas au statut
  (sinon la saisie en cours est écrasée).
- Aucune police téléchargée : le boîtier peut être hors ligne.
- Composants tiers embarqués : Vue et qrcode-generator, sous licence MIT (voir `NOTICE.md`), rappelés en tête de
  `app.js`.
