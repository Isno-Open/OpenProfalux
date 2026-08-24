# OpenProfalux — Plan test empirique appairage

## Statut du modèle DEVMEL (mise à jour)

DEVMEL crée bien une identité virtuelle PFX, mais cela ne prouve pas à lui seul
un apprentissage Chamberlain. Le runtime observé sélectionne une identité parmi
63 slots (`serial = (slot << 12) | 0x067`) et une clé KeeLoq correspondante.
La trame `PAIRMODE` validée pour le slot 54 est `serial=0x36067`,
`counter=2`, `hop=0xF029775B`, qui se déchiffre en `0x00670002` avec la clé
du slot. Aucune clé 64 bits ni seed distincte n'a été observée sur l'air.

Le retest doit donc distinguer trois hypothèses :

1. **Identité préprovisionnée / dérivation commune** : seule la clé DEVMEL
   associée au serial fonctionne.
2. **Self-learn propriétaire de type Chamberlain** : une clé arbitraire avec
   le même serial fonctionne également.
3. **Séquence manquante** : aucune des deux ne fonctionne car un échange ou une
   étape propriétaire supplémentaire est nécessaire.

Le firmware doit être vérifié avant émission : table 63 slots, index `>>12`,
codeword HCS300 LSB-first, discrimination 10 bits, TE≈450 µs et préambule
≈23 éléments. Un simple aller-retour logiciel KeeLoq ne constitue pas une
preuve d'appairage moteur.

### Piège critique : rejeu brut et trame générée sont deux chemins différents

Le rejeu fonctionnel ne valide pas `pfx_frame_build()`. Le rejeu transmet directement
les 66 bits capturés par `cc1101_tx_raw_bits()` ; leur ordre est donc déjà correct.
Une nouvelle identité, en revanche, construit ses champs avant de les émettre.

Tous les champs HCS300 sont transmis LSB-first, y compris les quatre bits bouton :

```text
[hopping 32 LSB-first] [serial 28 LSB-first] [button 4 LSB-first] [VLOW] [RPT]
```

Bug trouvé le 2026-08-24 par comparaison avec trois trames DEVMEL Lecanard38
(`counter=0xCEB`, `0xCEC`, `0xCF3`) : hopping et serial concordaient, mais le bouton
était empaqueté MSB-first. Cela permutait `0x1↔0x8` et `0x2↔0x4`. Le bouton clair
ne correspondait alors plus au bouton chiffré dans le hopping, donc une commande
générée pouvait être rejetée alors que les rejeux fonctionnaient parfaitement.

Le cas `button=0x0` masque totalement ce bug. Par conséquent, une injection d'apprentissage
en bouton zéro peut être correcte tandis que le test final `UP/DOWN/STOP` est invalide.
Toujours valider le codeword **sur l'air**, champ clair compris, avec un bouton non nul.

### Le placeholder d'enrôlement est levé

La capture DEVMEL d'enrôlement donne un plaintext KeeLoq
`0x50670003` : bouton spécial `0x5`, discrimination `0x067`, compteur `3`.
Ce `0x5` n'est pas le bouton `PROG/STOP+P` `0x8` d'une vraie télécommande.

Le binaire `AirSendWebService` confirme l'emplacement du champ : juste avant la
boucle KeeLoq (`0x7F5024`), il lit un octet de l'objet radio, le décale de 12 bits,
l'assemble avec la discrimination, puis décale l'ensemble de 16 bits. Les appels
isolés `PROG`, `STOP` et `PAIRMODE` du banc donnent un objet à zéro et ne suffisent
donc pas à reproduire la branche d'enrôlement complète de l'application.

OpenProfalux distingue désormais `PFX_BTN_ENROLL=0x05` (identité virtuelle DEVMEL)
de `PFX_BTN_PROG=0x08` (télécommande physique), et la procédure locale d'enrôlement
émet désormais la paire observée : `SETTINGS btn=0/counter=2` répété avec un
codeword fixe, relâchement, puis `ENROLL btn=5/counter=3` répété avec un codeword
fixe. Le compteur ne progresse qu'entre les deux commandes, pas entre leurs
répétitions.

#### Procédure radio à implémenter

État initial : l'identité virtuelle PFX est déjà choisie dans le pool DEVMEL,
avec `serial & 0xFFF == 0x067`, sa clé du slot et un compteur initial `2`.

| Étape | Commande logique | Bouton | Compteur | Plaintext pour `0x067` | Répétition |
|---|---|---:|---:|---:|---|
| 1 | SETTINGS / préparation | `0x0` | `2` | `0x00670002` | même codeword pendant environ 5 s ; `RPT=0`, puis `RPT=1` |
| 2 | relâchement | — | — | — | silence d'environ 300 ms |
| 3 | ENROLL / validation | `0x5` | `3` | `0x50670003` | même codeword répété ; `RPT=0`, puis `RPT=1` |

Règles impératives :

- chiffrer une seule fois par étape et répéter le même hopping ;
- ne pas incrémenter le compteur entre les répétitions d'un même appui ;
- incrémenter une seule fois au relâchement, donc `2 → 3` entre SETTINGS et ENROLL ;
- sérialiser hopping, serial et nibble bouton LSB-first ;
- ne jamais remplacer `0x5` par `0x0` ou `0x8` lors de l'étape 3.

Dans le firmware, `on_pair()` réalise cette séquence avec
`pfx_emit_hold(..., PFX_BTN_SETTINGS, 5000)`, une pause de 300 ms, puis
`pfx_emit_command(..., PFX_BTN_ENROLL)`.

La chorégraphie effectuée ensuite avec la vraie télécommande reste une étape
distincte : elle met le moteur dans les conditions mécaniques attendues, mais
elle ne remplace aucune des deux commandes de la nouvelle identité.

Ce document décrit la procédure de test empirique révisée après la [contre-analyse « 10e homme »](CONTRE-ANALYSE.md) qui a affaibli l'hypothèse initiale d'un simple "émets crypt_key random et le moteur enregistre".

## Rappel de la faiblesse identifiée

Une trame HCS301 ne transmet pas la clé secrète 64-bit. Un ciphertext 32-bit peut être produit par ~2^32 clés compatibles pour une paire plaintext/ciphertext donnée. **Le moteur ne peut pas déduire directement la crypt_key d'un seul bloc chiffré**.

Donc si l'appairage marche vraiment sans MFG key partagée, il existe **nécessairement** :
- Soit un canal séparé qui transmet la clé (=séquence propriétaire, trames hors format HCS301)
- Soit un stock d'identités préprovisionnées côté BOX AirSend
- Soit une dérivation faible côté Profalux (=clé calculable depuis serial seul)
- Soit une capacité crypto dans un firmware radio séparé non-analysé

## Plan test empirique révisé — 4 phases

### Phase 0 — Setup hardware (=vendredi 8 août ~1h)

1. Soudure E07-900M10S selon [WIRING.md](WIRING.md)
2. Câblage régulateur 3.3V + M5Stack ATOM Lite
3. Flash firmware OpenProfalux via docker
4. Vérifier `PARTNUM=0x00` au log série

### Phase 1 — Capture RX passive AVANT tout TX (=priorité absolue)

**But** : caractériser exactement ce que la BOX AirSend émet pendant sa procédure d'appairage, sans polluer avec notre propre TX.

**Procédure** :
1. Démarrer OpenProfalux en mode RX (=`openprofalux/listen/start` MQTT)
2. Rien d'autre — pas de burst TX
3. Simuler une procédure d'appairage classique :
   - Prendre télécommande Profalux d'origine EMPX-B1
   - Basculer switch P/N vers **P**
   - Presser **STOP** 5 sec
   - Moteur aller-retour de confirmation
   - Rebasculer switch vers **N**
4. Capturer TOUTES les trames pendant cette séquence

**Observables** :
- Nombre total de trames
- Durée séquence
- Longueur bit de chaque trame (=66 bits HCS301 standard, ou différentes ?)
- Modulation (=OOK ou changement en cours ?)
- Débit / bit-time
- Structure trame (=préambule, header, payload)
- Serial visible
- Rolling counter progression
- **Y a-t-il des trames HORS format HCS301 ?** — question critique

**Interprétation** :
- **Toutes trames = 66-bit HCS301 standard** → hypothèse E (=transfert clé propriétaire) affaiblie
- **Trames hors format présentes** → hypothèse E confirmée, il faut reverse ce format
- **Séquence longue avec pattern répétitif** → hypothèse préprovisionnement (=matching multiple serials essayés)

### Phase 2 — Test 1 : émission avec identité contrôlée

**But** : tester si le moteur accepte une paire (serial, crypt_key) arbitraire.

**Procédure** :
1. Générer runtime dans ESP32 :
   - `serial = 0x0A5F2C41` (=valeur contrôlée connue)
   - `crypt_key = 0x123456789ABCDEF0` (=valeur contrôlée, testable côté user)
   - `counter = 0`
2. Trigger appairage via HA (=`openprofalux/volet_test/pair`)
3. **Immédiatement** : sur télécommande d'origine, procédure PROG / P+STOP
4. Moteur en mode learn 60s
5. ESP32 émet burst 60 trames pendant fenêtre
6. Observer résultat

**Interprétation** :
- Moteur enregistre → hypothèse Chamberlain naïve (=D "dérivation faible" ou vraie acceptation arbitraire)
- Moteur ignore → hypothèse E ou A (=besoin séquence propriétaire ou firmware séparé)

### Phase 3 — Matrice discriminante (=si Phase 2 échoue)

Ta table de la contre-analyse section 8, Test 5 :

| # | Serial | Seed | Clé | Counter | Résultat |
|---|--------|------|-----|---------|----------|
| 1 | A     | X    | K1  | 1       | ?        |
| 2 | A     | X    | K2  | 1       | ?        |
| 3 | A     | Y    | K1  | 1       | ?        |
| 4 | B     | X    | K1  | 1       | ?        |
| 5 | B     | Y    | K2  | 1       | ?        |

**Interprétation** :
- Toutes passent → apprentissage direct ou validation très faible
- Une seule relation passe → dérivation attendue
- Seul un serial particulier passe → identité préprovisionnée ou whitelist
- Aucune ne passe → secret fabricant ou séquence propriétaire manquante

### Phase 4 — Reverse séquence propriétaire (=si nécessaire)

Si Phase 2-3 échouent ET Phase 1 a révélé des trames hors format HCS301 :

1. Analyser structure trames non-HCS301 :
   - Longueur bits
   - Pattern début (=magic bytes)
   - Champs (=serial ? key parts ? counter ?)
2. Comparer avec la doc KEELOQ Programming System (=DS51036D Microchip) qui décrit les commandes propriétaires
3. Reproduire la séquence dans le firmware OpenProfalux
4. Retester Phase 2 avec la séquence complète

## Priorité absolue = Phase 1

**Ne pas sauter la Phase 1 pour aller directement à la Phase 2 "burst brute force"**. Sans caractériser d'abord ce que la BOX AirSend émet, on tire à l'aveugle et un échec de Phase 2 ne discrimine rien.

## Après validation empirique

Selon les résultats, mettre à jour :
- `CONTRE-ANALYSE.md` avec les hypothèses éliminées et celles renforcées
- `docs/RESEARCH.md` avec les preuves empiriques ajoutées
- `firmware/main/profalux.c` selon la vraie procédure identifiée

---

## Update 2026-08-06 — décision Phase 1 basée sur observations

Suite au fork "piste logicielle forcée" qui a révélé `Channel::setSeed()`, la Phase 1 doit **spécifiquement chercher** :

1. **Longueur trames observées** — 66-bit HCS301 standard OU longueur étendue ?
2. **Champ seed visible** — si une trame contient 32-60 bits identifiables comme seed en clair (=pattern non-encrypted au milieu)
3. **Ordre des trames** — seed émis d'abord, puis trames rolling, ou l'inverse ?
4. **Structure protocole** — trames identiques répétées (=simple burst) ou séquence structurée (=state machine) ?

Table décisionnelle Phase 1 :

| Observation | Diagnostic | Suite recommandée |
|-------------|-----------|-------------------|
| Toutes trames 66-bit identiques structurellement | Chamberlain Self-Learn stricte possible | Phase 2 : émettre burst identique avec clé random |
| Séquence de 2-3 trames différentes en début | Secure Learn variant probable | Reverse structure trame seed, implémenter dans firmware |
| Trames hors format HCS301 (=magic, structure inconnue) | Protocole propriétaire | Reverse complet nécessaire avant test empirique |
| Peu de trames + long silence | Communication cloud ou firmware radio | Vérifier hypothèse A/C via test réseau isolé |

**Ne pas court-circuiter Phase 1** — l'échec de la théorie initiale rend l'observation empirique **plus critique** que jamais.

---

## Rétractation 2026-08-06 sur Phase 1 diagnostic

La table décisionnelle Phase 1 mentionnait "Séquence 2-3 trames différentes en début → Secure Learn variant (=seed transmis en clair)" basée sur la découverte `setSeed()`. Cette découverte étant **rétractée** (=setSeed est un setter trivial), l'interprétation doit être neutralisée :

| Observation RX passive | Diagnostic révisé |
|-----------------------|-------------------|
| Toutes trames 66-bit HCS301 identiques | Chamberlain stricte possible OU firmware BOX fait crypto en amont |
| Séquence 2-3 trames différentes en début | **Structure propriétaire à reverser** (=pas assumer Secure Learn) |
| Trames hors format HCS301 | Protocole propriétaire, reverse nécessaire |
| Peu de trames + silence | Cloud ou firmware radio séparé (=hypothèses A/C) |

L'hypothèse A (=firmware BOX physique fait la crypto) étant maintenant la plus probable (=30%), il est important d'observer **si la BOX AirSend fait un burst RF cohérent ou si elle attend des instructions du cloud/service**.
