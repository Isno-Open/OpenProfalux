# Consignes pour les agents (Codex, revues automatiques)

Ce fichier dit ce que le projet attend du code. Il sert aux revues automatiques des pull requests :
chaque point ci-dessous est un défaut à signaler, au même titre qu'un bogue.

## Review guidelines

### Pas de texte littéral comparé à la main

Une valeur du domaine (un état, un type, une marque, un mode, un statut) ne se compare jamais à une
chaîne écrite à la main. Elle appartient à une énumération déclarée une fois, et le code s'y réfère
par son nom.

```ts
// à signaler
if (etat === 'occupee') { ... }
if (d.brand == 'somfy') { ... }

// attendu
export const RADIO_STATE = { busy: 'occupee', idle: 'libre' } as const;
export type RadioState = (typeof RADIO_STATE)[keyof typeof RADIO_STATE];
if (state === RADIO_STATE.busy) { ... }
```

Une union de littéraux (`type X = 'a' | 'b'`) ne suffit pas : le code continue d'écrire `'a'` à la
main. Même règle en C (une `enum` ou une constante nommée, pas un `strcmp` contre un littéral épars).
Seuls les `typeof x === 'string'` et équivalents restent tels quels.

### Le code en anglais, les commentaires en français

Les identifiants sont en anglais : fonctions, variables, types, composants, fichiers, champs. Un nom
en français (`lireEtat`, `sonder`, `reelle.js`) ou hybride (`lireCarte`) est à signaler. Les
commentaires et la documentation sont en français, et le texte affiché à l'utilisateur aussi.

Exception : les valeurs qui partent sur le fil (noms de champs JSON échangés avec le firmware, ancres
d'URL, valeurs d'une énumération lue par un autre programme) sont des données, pas des identifiants.

### L'interface web est en TypeScript

Tout le front (`ui/`) est en TypeScript : `<script setup lang="ts">` dans les composants, `.ts` pour
les modules, `tsconfig.json` en `strict` avec `noUncheckedIndexedAccess`. Pas de JavaScript avec
JSDoc. `satisfies` plutôt que `as` : une assertion fait taire le compilateur au lieu de vérifier.

### Pas de valeur matérielle en dur

Une valeur qui décrit une carte ou une radio (broches, fréquences, taille de flash, présence d'une
radio) se déclare dans `boards/<carte>.json` et le code la lit. Une valeur écrite en dur casse en
silence sur une autre carte.

### Un contrôle doit savoir échouer, et être appelé

Un test ou une vérification qui ne peut pas échouer, ou que personne n'appelle, ne protège rien.
Signaler un contrôle ajouté sans appel, ou un banc qui ne tourne pas dans la CI.

### Écriture

Pas de tiret cadratin ni demi-cadratin (U+2014, U+2013) dans le code, les commentaires, la documentation
ou les messages : reformuler (virgules, parenthèses, deux-points).
