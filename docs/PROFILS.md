# Profils, protocoles et types

Ce document décrit comment OpenProfalux sait piloter un appareil : ce qui est du code, ce qui est une donnée, et qui
déclare quoi. Il fixe le modèle de l'issue #134. Un utilisateur doit pouvoir ajouter une marque ou un appareil sans
nouveau firmware, tant que l'encodeur existe.

> **État** : la section 0 décrit ce que fait le code aujourd'hui (état `270add2`, fusion de la #133). Les sections 1
> à 8 décrivent le modèle **visé**. La section 9 dit l'écart entre les deux.

## 0. Ce que fait OpenProfalux aujourd'hui

Résumé de l'inventaire complet, [INVENTAIRE-270add2.md](INVENTAIRE-270add2.md), où chaque point renvoie au
`fichier:ligne` du code de `270add2`. Ces renvois valent pour ce commit, pas pour la suite.

### 0.1 Qui lit `profiles.json`

Le firmware embarque `firmware/main/web/profiles.json` et le sert sur `/profiles.json`, mais **ne le lit jamais**.
C'est l'interface qui le lit et recopie ses valeurs dans les requêtes ou dans la fiche de l'appareil. Le C n'en garde
que des miroirs écrits à la main (`dev_type.h`, `cover_attrs.c`), vérifiés par le banc `cover-attrs`.

Le fichier contient `devices_max`, `name_max`, `roles` (centrale, zone), `params` (statut de chaque trait), `types`
(`volet` code 0, `portail` code 1, avec `dpad`, `capture`, `positional`, `travel_s`) et six `profils` aux clés
françaises (`marque`, `mecanisme`, `flow`, `nom`) :

| Profil | Type | Parcours (`flow`) | Bande |
|---|---|---|---|
| CAME | portail | `rejeu` | 433 |
| Profalux, cloner | volet | `learn` | 868 |
| Profalux, Eveno, NeoSol, enrôlement | volet | `enrol-pfx` | 868 |
| Somfy RTS | volet | `create-somfy` | 433 |

Lus nulle part : les `note`, `endpoint` de Somfy (l'adresse est en dur dans l'interface), et pour CAME
`tx.encoder`, `short_us`, `long_us`, `repeats`, `gap_us`. Les notes du fichier sur `rx` et `repeats` disent l'inverse
du code : `rx` part bien au firmware, `repeats` et `gap_us` non.

### 0.2 Par protocole

| | Clone KeeLoq | Enrôlement KeeLoq | Somfy RTS | Rejeu CAME |
|---|---|---|---|---|
| Écran | `CaptureFlow.vue` | `EnrolFlow.vue` | `SomfyFlow.vue` | `CaptureFlow.vue` |
| Réglé par appareil | fréquence, TE, bit de répétition, RMT | idem, et la marque | `frame_bits` (56 ou 80), fréquence | fréquence |
| En dur | 10 trames par appui, codage sur l'air, codes 2, 4, 8 | apprentissage code 5, 9 trames × 5 salves ; 4 modèles et leurs routines R6, R7, R8 dans `pfx_enrol.c` ; les gestes dans `EnrolFlow.vue` | 2 trames, timings, codes des boutons | 8 répétitions, silence 12 000 µs |

Défauts relevés :

- un volet Somfy naît **sans marque, avec la fréquence 0 et `frame_bits` 0** : son écran n'envoie que l'adresse ;
- le rejeu CAME rejoue la première trame capturée pour toute commande qui n'en a pas ;
- l'encodeur CAME (`came_tx.c`) est compilé mais **jamais appelé**.

### 0.3 Le type d'un appareil

Un numéro dans la fiche (0 volet, 1 portail), modifiable seul depuis l'édition, sans lien avec le profil.
L'enrôlement et Somfy ne le transmettent pas : un appareil enrôlé naît toujours volet. Le type ne change que
l'interface et le cloud :

- **Home Assistant** reçoit toujours `device_class: shutter`.
- **L'émission** ne dépend pas du type.
- **Le firmware** décide si un appareil a une position sans regarder son type : un portail étalonné serait publié
  positionnel.

### 0.4 Centrale et zone

Les règles diffèrent entre le firmware et l'interface. Pour la zone, l'interface n'accepte que les types positionnels,
et cette règle y est écrite en dur ; le firmware ne regarde pas le type.

### 0.5 Bande et puissance

La fréquence globale d'une radio est contrôlée contre `bands_allowed_khz`. La **fréquence par appareil ne l'est
pas** : ni à l'écriture, ni à l'émission. Si aucune radio ne couvre la fréquence, la radio active est recalée au lieu
de refuser. `max_dbm` n'est lu par aucun code.

## 1. Cinq niveaux

| Niveau | Nature | Ce qu'il porte | Exemples |
|---|---|---|---|
| **encodeur** | du code | ce qui se calcule : cryptographie, clés, fabrication des trames | `keeloq`, `somfy-rts`, `io-homecontrol`, `ook-fixe`, `rejeu-brut` |
| **protocole** | une donnée, sur un encodeur | la structure de la trame, les commandes, la trame d'apprentissage, la procédure d'ajout, ce qu'il calcule lui-même | `profalux`, `rts`, `io`, `came` |
| **type** | une donnée | une famille d'appareils : son entité Home Assistant et les commandes qu'elle exige | `volet`, `portail` |
| **profil** | une donnée, sur un protocole et un ou plusieurs types | une marque, la radio, les boutons et ce que chacun émet, le statut de test | Profalux, Franciaflex, Somfy, Velux, Portail CAME |
| **appareil** | la fiche sur le boîtier | son profil, son identité, son compteur, ce qui a été mesuré chez lui | « Volet chambre » |

Seuls les **encodeurs** demandent du firmware. Tout le reste se déclare.

Le mot « moteur » est réservé au moteur de l'appareil (celui du volet ou du portail, qui apprend une télécommande).

## 2. L'encodeur

L'encodeur est le savoir-faire écrit en C : coder des bits en durées OOK, chiffrer KeeLoq, obscurcir une trame RTS,
chiffrer io, rejouer une capture. Un encodeur sert à plusieurs protocoles : `ook-fixe` sert CAME, Nice FLO, EV1527 et
d'autres, avec des durées et des nombres de bits différents.

**Aucune clé ni aucune identité ne sort d'un encodeur.** Les profils et les protocoles sont publics (`/profiles.json`
est servi par le boîtier, et le dépôt est ouvert).

## 3. Le protocole

Le protocole est la recette d'une famille de télécommandes, posée sur un encodeur.

| Il déclare | Exemple pour `profalux` |
|---|---|
| son encodeur | `keeloq` |
| la structure de la trame | préambule, en-tête, codage des bits |
| ses commandes et leur code | monter 2, stop 4, descendre 8, apprentissage 5 |
| s'il impose ses boutons (`buttons`, voir 3.1) | `fixed` |
| la trame d'apprentissage (`enrol`) | code 5, 9 trames par rafale, 5 rafales, compteur de départ 3 |
| la procédure d'ajout (`procedure`, voir 3.2) | identifier, émettre, gestes de validation, confirmer, tester |
| ce qu'il calcule lui-même | l'identité neuve ; pour io, la destination |

### 3.1 Boutons imposés ou libres

Deux sortes de protocoles, et c'est le protocole qui le dit :

- **`"buttons": "fixed"`** (`profalux`, `rts`, `io`) : le récepteur ne comprend que les commandes du protocole. Le
  protocole déclare donc ses boutons. Un profil ne peut que **choisir et présenter** : lesquels montrer, avec quel
  libellé, à quelle place. Il ne peut pas en inventer.
- **`"buttons": "free"`** (`came`, et tout ce qui passe par `rejeu-brut` ou `ook-fixe`) : les boutons sont ceux de la
  télécommande d'origine. C'est le **profil** qui les déclare, et chacun se remplit par une capture (ou par un code
  décodé pour `ook-fixe`).

### 3.2 La procédure d'ajout

La procédure est le déroulé montré à l'utilisateur pour ajouter un appareil. C'est une liste d'**étapes**. Le genre
d'une étape est du code (l'écran d'ajout sait le dérouler) ; son texte et son enchaînement sont des données.

| Étape | Ce qu'elle fait |
|---|---|
| `identify` | capte la télécommande d'origine : le protocole reconnaît le modèle et choisit l'identité |
| `instruction` | montre un texte (un geste à faire) et attend « c'est fait » |
| `capture` | capte un bouton de la télécommande d'origine (méthode copie) |
| `emit-enrol` | émet la trame d'apprentissage ; `"retry": false` interdit de la réémettre (batterie des moteurs solaires) |
| `confirm` | demande si le moteur a répondu (un va-et-vient) |
| `test` | fait essayer les boutons |

Quand les gestes dépendent du modèle, la procédure déclare des **variantes** (`procedure.variants`), et l'étape
`identify` choisit laquelle suivre. Profalux en a trois aujourd'hui (R6, R7, R8).

| Protocole | Procédure |
|---|---|
| `profalux` | `identify` → `emit-enrol` → `instruction` (gestes de la variante) → `confirm` → `test` |
| `rts` | `instruction` (appui long sur PROG de la télécommande d'origine) → `emit-enrol` → `confirm` → `test` |
| `io` | `instruction` (appui long sur RESET de la télécommande d'origine) → `emit-enrol` (une seule fois) → `confirm` → `test` |
| `came` | `capture` de chaque bouton → `test` |

Un seul écran d'ajout déroule toutes les procédures, y compris celles qu'un utilisateur déclare.

### 3.3 Copie ou enrôlement

La **méthode** (`add`) dit comment l'appareil est ajouté :

- **copie** (`copy`) : le boîtier reprend l'identité d'une télécommande existante (clone KeeLoq, ou capture d'un code
  fixe) ;
- **enrôlement** (`enrol`) : le boîtier crée une identité neuve que le moteur apprend, par la trame d'apprentissage.

Un même encodeur peut servir les deux : `profalux` existe en copie et en enrôlement.

## 4. Le type

Le type est une famille d'appareils, partagée par tous les profils de cette famille. Il ne connaît aucune trame.

| Il déclare | Exemple pour `volet` | Exemple pour `portail` |
|---|---|---|
| l'entité Home Assistant | `cover`, classe `shutter` | `cover`, classe `gate` |
| s'il a une position | oui | non |
| la durée de course possible | 1 à 180 s | 1 à 180 s |
| les commandes exigées | ouvrir, arrêter, fermer | ouvrir, arrêter, fermer |
| des boutons par défaut | ▲ ■ ▼ | un bouton pas à pas |

Les familles d'entités (`cover` positionnel, `cover` sans position, `button`, `switch`, `light`) sont du code : le
firmware sait les annoncer à Home Assistant. Un type nouveau d'une famille existante (porte de garage, prise) se
déclare sans firmware.

Le projet déclare des types ; un utilisateur pourra en ajouter. Un type se désigne par sa **clé** (`volet`,
`portail`), jamais par un numéro.

## 5. Le profil

Le profil est ce que l'utilisateur crée et partage. Il déclare :

- son **protocole** et sa **méthode** ;
- le ou les **types** qu'il gère ;
- une **marque**, facultative (sans marque, c'est un profil générique, par exemple « volet à code fixe ») ;
- la **radio** : bande, fréquence, modulation, réglage de réception ;
- l'**émission** : répétitions, intervalle ;
- ses **boutons** : libellé, place, commande et comportement, et ce que chacun émet ;
- son **statut de test** ;
- éventuellement sa **base** (`inherits`) : il ne contient alors que ce qu'il remplace.

Un profil qui ne déclare pas de boutons prend ceux de son type (protocole libre) ou ceux de son protocole (protocole
à boutons imposés).

### 5.1 Un bouton

| Champ | Sens | Exemple |
|---|---|---|
| `id` | identifiant stable du bouton | `main` |
| `label`, `position` | ce que voit l'utilisateur | « Portail », ligne 0 colonne 0 |
| `cmd`, `behavior` | ce qu'il fait pour Home Assistant | `step` |
| la trame | une commande du protocole, ou une capture | `{ "source": "capture", "repeats": 8, "gap_us": 12000 }` |
| `cycle`, `press_gap_ms` | pour un bouton pas à pas : ce que fait chaque appui, et le délai entre deux appuis enchaînés | `["open", "stop", "close", "stop"]`, 3000 |

### 5.2 Exemples

```
type « volet » : cover positionnel, exige ouvrir / arrêter / fermer, boutons par défaut ▲ ■ ▼
protocole « profalux » (encodeur keeloq, boutons imposés) : monter 2, stop 4, descendre 8, apprentissage 5, procédure
profil « Nouvelle télécommande Franciaflex » : hérite de « Nouvelle télécommande Profalux », marque Franciaflex,
    fréquence 868,425
profil « Portail CAME » : type portail, protocole came (boutons libres), marque CAME,
    bouton « Portail » : capture, pas à pas, cycle ouvre / stop / ferme / stop
appareil « Volet chambre » : profil, identité, compteur
```

## 6. L'appareil

L'appareil **suit son profil**. Sa fiche ne garde que :

- l'identifiant de son **profil** ;
- son **identité** : numéro de série, adresse, compteur (privés, jamais dans un profil) ;
- les **trames capturées** chez lui (méthode copie) ;
- **ce qui a été mesuré chez lui** : fréquence calée, temps de course, position, cycle pas à pas en cours.

Son type et ses boutons se lisent dans son profil. Modifier un profil change tous les appareils qui l'utilisent.

## 7. Les contrôles

Faits par le générateur (`tools/gen_profiles.py`) pour le catalogue du projet, et par le boîtier pour un profil
utilisateur, contre le **même schéma** :

- l'encodeur du protocole existe dans ce firmware ;
- chaque commande exigée par le type est portée par un bouton du profil ;
- chaque bouton émet une commande que le protocole connaît, ou une capture ;
- un protocole à boutons imposés : le profil ne déclare que des boutons du protocole ;
- un bouton pas à pas a un cycle et un délai entre appuis, et ses actions sont connues ;
- la fréquence est dans les bandes que la carte autorise (`bands_allowed_khz`) ;
- une base (`inherits`) existe, sans boucle, et ne change ni l'encodeur ni la bande.

Au pilotage, une commande qu'aucun bouton de l'appareil ne porte est **refusée**, jamais acquittée sans émission.

## 8. Créer un profil

1. Choisir ou créer le type.
2. Choisir le protocole : existant, ou nouveau sur `ook-fixe` ou `rejeu-brut` pour un code fixe.
3. Déclarer ou choisir les boutons, selon que le protocole les impose ou non.
4. Capturer chaque bouton : le boîtier en déduit la radio et l'émission.
5. Essayer, enregistrer (« non testé »), passer à « testé » quand l'appareil répond.
6. Exporter, ou proposer au projet par « Ajouter à OpenProfalux ».

## 9. Où en est le code

| Point du modèle | Aujourd'hui |
|---|---|
| encodeurs | `keeloq`, `somfy-rts`, `rejeu-brut` ; `came_tx.c` existe mais n'est pas appelé ; `io-homecontrol` (PR #147, fermée) et `ook-fixe` à écrire |
| protocoles | n'existent pas : leur contenu est en dur dans le C et dans les écrans (Eveno et NeoSol recopient Profalux) |
| boutons | ni dans le profil, ni dans le protocole : `dpad` et `capture` du **type**, codes en dur dans le C |
| types par profil | un seul (`type`) |
| type de l'appareil | un numéro dans la fiche, modifiable seul, sans lien avec le profil ; ne va pas jusqu'à Home Assistant |
| procédures d'ajout | une par écran (`CaptureFlow.vue`, `EnrolFlow.vue`, `SomfyFlow.vue`), choisie par `flow` ; les modèles Profalux dans le C, leurs gestes dans `EnrolFlow.vue` |
| héritage | absent |
| schéma | absent |
| valeurs du profil | lues par l'interface seule ; une partie jamais lue (voir 0.1) |
| commande sans bouton | acquittée sans émission, ou rejoue une autre trame (CAME) |
| portail pas à pas | absent (repris de la PR #146, fermée) |
| bande par appareil | non contrôlée |
