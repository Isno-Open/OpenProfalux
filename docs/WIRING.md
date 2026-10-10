# OpenProfalux — Wiring convention couleurs fils

Convention couleurs standard pour souder **8 fils** entre **EBYTE E07-900M10S** et **M5Stack ATOM Lite** (=cible production) ou **ESP32 DevKit** (=prototype alternatif).

## Convention couleurs

Standard électronique universel : rouge/noir pour alim, signaux SPI dans ordre visuel type câbles Ethernet.

| # | Fil | Signal | Rôle |
|---|-----|--------|------|
| 1 | 🔴 **Rouge** | VCC | Alimentation 3.3V |
| 2 | ⚫ **Noir** | GND | Masse |
| 3 | 🟡 **Jaune** | MOSI | SPI data out (=ESP → E07) |
| 4 | 🟢 **Vert** | MISO | SPI data in (=E07 → ESP) |
| 5 | 🔵 **Bleu** | SCK | SPI clock |
| 6 | ⚪ **Blanc** | CSN | SPI chip select |
| 7 | 🟠 **Orange** | GDO0 | Data async OOK (=KEELOQ critique) |
| 8 | 🟣 **Violet** | GDO2 | Debug (=optionnel) |

## Table complète — Target M5Stack ATOM Lite (=Dupont-friendly)

**Utilisation** : Dupont femelle-femelle direct sur bottom header ATOM Lite.

| Signal | Couleur | Pin E07 | Pin M5Stack ATOM Lite |
|--------|---------|---------|-----------------------|
| VCC | 🔴 Rouge | **9** (=rangée HAUT milieu) | **5V bottom → régulateur 3.3V externe** ⚠️ |
| GND | ⚫ Noir | **20** (=rangée BAS droite) | **GND** (=bottom) |
| MOSI | 🟡 Jaune | **17** (=rangée BAS milieu) | **G23** (=bottom) |
| MISO | 🟢 Vert | **16** (=rangée BAS milieu) | **G33** (=bottom) |
| SCK | 🔵 Bleu | **18** (=rangée BAS milieu) | **G19** (=bottom) |
| CSN | ⚪ Blanc | **19** (=rangée BAS milieu) | **G22** (=bottom) |
| GDO0 | 🟠 Orange | **15** (=rangée BAS milieu) | **G25** (=bottom) |
| GDO2 (opt) | 🟣 Violet | **14** (=rangée BAS milieu-gauche) | **G21** (=bottom) |

## Table complète — Target External (=ESP32 DevKit alternatif)

| Signal | Couleur | Pin E07 | Pin ESP32 DevKit |
|--------|---------|---------|------------------|
| VCC | 🔴 Rouge | 9 | 3.3V |
| GND | ⚫ Noir | 20 | GND |
| MOSI | 🟡 Jaune | 17 | GPIO23 |
| MISO | 🟢 Vert | 16 | GPIO19 |
| SCK | 🔵 Bleu | 18 | GPIO18 |
| CSN | ⚪ Blanc | 19 | GPIO5 |
| GDO0 | 🟠 Orange | 15 | GPIO4 |
| GDO2 | 🟣 Violet | 14 | GPIO2 |

## Table complète — D1 mini ESP32 (`-DBOARD=d1-mini`)

Même bus SPI que le DevKit, mais **GDO0 sur IO26** : sur la carte de référence, IO4 n'est
pas câblé. SPI à 1 MHz (voir `boards/d1-mini.json`).

| Signal | Couleur | Broche D1 mini ESP32 |
|--------|---------|----------------------|
| VCC | 🔴 Rouge | 3V3 |
| GND | ⚫ Noir | GND |
| MOSI | 🟡 Jaune | IO23 |
| MISO | 🟢 Vert | IO19 |
| SCK | 🔵 Bleu | IO18 |
| CSN | ⚪ Blanc | IO5 |
| GDO0 | 🟠 Orange | IO26 |

**Compile flag** :
- `CONFIG_OPENPROFALUX_TARGET_M5STACK=y` → M5Stack ATOM Lite
- `CONFIG_OPENPROFALUX_TARGET_EXTERNAL=y` → ESP32 DevKit

## Alimentation 3.3V (M5Stack ATOM Lite)

**CORRECTION 2026-08-09** : le M5Stack ATOM Lite **expose bien un pin `3V3`** (coin haut-gauche
du bottom header, confirmé sur la sérigraphie de la carte). On alimente donc le module RF
**directement en 3V3, régulateur NON nécessaire**.

⚠️ Le CC1101 supporte **1.8-3.6V max** → **jamais** le brancher en 5V (destruction immédiate).
Toujours vérifier au multimètre que le pin utilisé sort bien ~3,3 V avant de connecter le VCC.
(Une mesure antérieure sur le pin **5V** avait fait croire à tort qu'il n'y avait pas de 3.3V.)

```
M5Stack ATOM 3V3 ──────────► module VCC 🔴
M5Stack ATOM GND ──────────► module GND ⚫
```

Régulateur 5V→3,3V (AMS1117 / HT7333) = **optionnel**, utile seulement si on alimente le module
depuis une source 5V sans 3V3 disponible (ce qui n'est pas le cas de l'ATOM Lite).
- **MP1584 mini buck** : plus efficace mais légèrement plus cher

**Ne PAS utiliser** :
- Pin 5V direct vers E07 → destruction immédiate CC1101
- Divisieur de tension résistif → instable, ondulation courant

L'ESP32 DevKit alternatif n'a pas ce problème — il expose bien 3.3V direct sur un pin dédié.

## Schéma E07-900M10S — position naturelle (=écriture face + IPX bas-droite)

```
              ── rangée HAUT (=pins 11 → 1 gauche→droite) ──
       ┌──────────────────────────────────────────────────────┐
       │  ●   ●   ●   ●   ●   ●   ●   ●   ●   ●   ●          │
       │ 11  10   9   8   7   6   5   4   3   2   1          │
       │      —  🔴  —   —   —   —   —   —   —   —          │
       │         V                                           │
       │         C                                           │
       │         C                                           │
       │                                                     │
       │            E07 900M10S                              │
       │            QR • FCC • CE                            │
       │                                                     │
       │                                        ⓘ IPX        │
       │                                                     │
       │  ●   ●   ●   ●   ●   ●   ●   ●   ●   ●   ●          │
       │ 12  13  14  15  16  17  18  19  20  21  22          │
       │      —  🟣  🟠  🟢  🟡  🔵  ⚪  ⚫   —   —          │
       │              G   G   M   M   S   C   G              │
       │              D   D   I   O   C   S   N              │
       │              O   O   S   S   K   N   D              │
       │              2   0   O                              │
       └──────────────────────────────────────────────────────┘
              ── rangée BAS (=pins 12 → 22 gauche→droite) ──
```

## Schéma M5Stack ATOM Lite — bottom header

```
                  ┌──────────────────────┐
                  │                      │
                  │  ▓▓▓ ATOM Lite ▓▓▓   │
                  │   ESP32-PICO-D4      │
                  │                      │
                  │      [LED]           │
                  │      [BTN]           │
                  └──┬─┬─┬─┬─┬─┬─┬─┬─┬──┘
                     │ │ │ │ │ │ │ │ │
                     G G G G G G 3 G G
                    21 25 22 19 33 23 . N 5
                     │ │ │ │ │ │ 3 D V
                     │ │ │ │ │ │ V   ← Grove/USB en haut
                     🟣🟠⚪🔵🟢🟡🔴⚫

    Ordre lecture bottom (=gauche→droite face avant) :
      Pin 1 : G21 = 🟣 GDO2 (opt.)
      Pin 2 : G25 = 🟠 GDO0
      Pin 3 : G22 = ⚪ CSN
      Pin 4 : G19 = 🔵 SCK
      Pin 5 : G33 = 🟢 MISO
      Pin 6 : G23 = 🟡 MOSI
      Pin 7 : 3.3V = 🔴 VCC (pin 3V3 du bottom header — alim directe du module RF)
      Pin 8 : GND = ⚫ GND
```

> **CORRECTION 2026-08-09** : l'ATOM Lite **expose bien un pin `3V3`** (coin haut-gauche,
> sérigraphie). Le VCC du module RF va **directement dessus, sans régulateur**. Les signaux
> SPI vont direct sur les GPIO (logique 3,3 V native ESP32). TOUJOURS vérifier au multimètre
> (DCV, rouge sur 3V3, noir sur GND) que le pin sort ~3,3 V avant de brancher le VCC.

**Attention** : l'ordre exact des pins sur le bottom header ATOM Lite peut varier selon la révision. Vérifier avec un multimètre ou la sérigraphie sur le PCB avant de brancher.

## Ordre soudure recommandé (=du plus critique au moins)

Côté E07 (=SMD 1.27mm castellated, soudure fine) :

1. **🔴 Rouge (VCC pin 9)** — priorité alim
2. **⚫ Noir (GND pin 20)** — priorité alim
3. **⚪ Blanc (CSN pin 19)** — SPI critique
4. **🔵 Bleu (SCK pin 18)** — SPI critique
5. **🟡 Jaune (MOSI pin 17)** — SPI critique
6. **🟢 Vert (MISO pin 16)** — SPI critique
7. **🟠 Orange (GDO0 pin 15)** — data KEELOQ critique
8. 🟣 Violet (GDO2 pin 14) — optionnel debug

Côté M5Stack ATOM Lite : **aucune soudure**, connecteurs Dupont femelle enfichés sur les pins mâles du bottom header.

## Notes soudure côté E07

- **Loupe Anpro 3.5X + LED** conseillée pour SMD 1.27mm
- **Support 3ème main** pour fixer le module
- **Étamer chaque pad** avec une micro-goutte avant d'y poser le fil
- **Fils AWG 28-30** courts (=~10cm) pré-étamés à une extrémité (=côté E07)
- **Autre extrémité** : sertir/souder connecteur Dupont femelle
- **Colle chaude** ou epoxy après soudure pour robustesse mécanique
- **Longueur fils** : garder ~10-15cm max, sinon parasites SPI possibles

## Test continuité après soudure

Avec multimètre en mode continuité :
- Vérifier 🔴 VCC (=fil rouge) → contre 3.3V ATOM Lite (=bip)
- Vérifier ⚫ GND (=fil noir) → contre GND ATOM Lite
- Vérifier chaque signal SPI → contre le pin ATOM Lite correspondant
- **Aucun court-circuit** entre 2 signaux (=silence multimètre entre pins adjacents E07)

## Vérification firmware (=après flash)

Au premier boot, log série doit afficher :
```
[cc1101] PARTNUM=0x00 (=expected 0x00 for CC1101)
```

Si `PARTNUM != 0x00` ou timeout :
- Vérifier MISO/MOSI **pas inversés** (=erreur la plus fréquente)
- CSN mal soudé ou fil coupé
- GND commun manquant entre E07 et ATOM Lite
- Alim 3.3V absente ou trop faible (=mesurer 3.15-3.35V au multimètre sur pin VCC E07)

## Précaution IPX

Connecteur **U.FL fragile** — **ne pas tirer sur le câble antenne** pour débrancher. Soulever le connecteur perpendiculairement au PCB. ~30 clip/déclip max avant usure.
