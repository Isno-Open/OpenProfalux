# Notices juridiques

Ces notices vivaient jusqu'au 2026-09-16 à la fin du fichier `LICENSE`. Elles en
ont été sorties parce que **GitHub ne reconnaissait plus la licence** : son
détecteur exige une correspondance stricte avec le texte MIT, et tout ajout à la
suite le fait basculer sur `NOASSERTION` / « Other ».

Ce n'était pas cosmétique. La chaîne d'admission ISNO place la licence en
**premier contrôle**, et elle a déjà refusé `ESP32Marauder` pour licence non
identifiable. Notre propre firmware était dans le même état : la règle se serait
appliquée à tout le monde sauf à nous.

La licence est **MIT**, texte intégral dans `LICENSE`.

## La ligne de copyright, corrigée le 2026-09-16

Elle disait « OpenKeeloq contributors », un reste du nom précédent du projet.
Vérifié avant de la changer : `git log` ne compte **qu'un seul auteur**, sur 124
commits. Personne d'autre ne détient de droits, la correction ne prive donc
personne. Elle nomme maintenant le titulaire réel, et laisse la place aux
contributeurs à venir.

## Brevet KeeLoq

Le brevet sur l'algorithme KEELOQ (US 5517187, Nanoteq/Microchip) a **expiré en
2016**. L'algorithme et la constante NLF `0x3A5C742E` sont dans le domaine public.

## Clés

Ce logiciel **ne contient ni ne distribue aucune clé secrète de fabricant**. Il
engendre des clés aléatoires par appareil et s'appuie sur les procédures
d'auto-apprentissage côté récepteur, documentées notamment par le brevet
Chamberlain US 5686904.

## Composants tiers embarqués

L'interface du boîtier (`ui/`) est construite avec Vite ; deux bibliothèques y sont embarquées, dans
`app.js`, qui rappelle leurs avis en tête de fichier. Versions figées par `ui/package-lock.json`.

- Vue 3.5.44 (https://github.com/vuejs/core), Copyright (c) 2018-present Yuxi (Evan) You and Vue
  contributors, licence MIT : le cadre de l'interface.
- qrcode-generator 2.0.4, QR Code Generator for JavaScript, Copyright (c) 2009 Kazuhiko Arase, licence
  MIT (https://github.com/kazuhikoarase/qrcode-generator) : le QR code de l'appairage cloud.
  « QR Code » est une marque déposée de DENSO WAVE INCORPORATED.
