# Mesures RF sur telecommande Profalux : 2026-08-10

Banc : ESP32-PICO-D4 + CC1101 (E07), cablage M5Stack ATOM Lite de [WIRING.md](WIRING.md).
Toutes les valeurs ci-dessous sont **mesurees**, jamais deduites d'une specification.

## Resultat principal : la porteuse n'est pas a 868,35 MHz

| Grandeur | Valeur mesuree | Ce que dit le depot |
|---|---|---|
| Frequence porteuse | **868,425 MHz** | 868,35 MHz (et 868,30 par le `FREQ0` du code) |
| Puissance en champ proche (20-30 cm) | -38 dBm | (rien) |
| Plancher de bruit | -94 dBm | (rien) |
| Marge | +52 a +56 dB | (rien) |
| Largeur du lobe a -10 dB | moins de 50 kHz | (rien) |

`FREQ2/1/0` correspondant : **`0x21 0x66 0xA5`**.

Consequence directe : `cc1101.c` pose `0x21/0x65/0x6A`, soit 868,300 MHz, avec
`MDMCFG4 = 0xF6` qui donne une bande passante RX de 58 kHz. La porteuse reelle
est donc **125 kHz au-dessus du centre d'un filtre large de 58 kHz** : elle
tombe entierement hors bande. A elle seule, cette erreur de calage suffit a
expliquer qu'aucune trame ne soit recue, independamment de toute question de
modulation.

## Methode

Le CC1101 mesure le RSSI dans le filtre de canal numerique, **en amont du
demodulateur**. Une mesure d'energie est donc valable quelle que soit la
modulation : elle ne prejuge ni de l'OOK ni du FSK.

Deux balayages successifs :

1. **Grossier** : 866 a 870 MHz, pas de 50 kHz, filtre 325 kHz.
   238 balayages, 34 positifs. Pic repete a 868,55/868,60 MHz.
   Profil troue et instable d'un balayage a l'autre : en modulation hachee,
   chaque point tombe au hasard sur une emission ou un silence.

2. **Fin, avec maintien du maximum** : 867,8 a 869,2 MHz, pas de 25 kHz,
   filtre 58 kHz, 20 ms de maintien par point, cumule sur 49 balayages.
   Le maintien du maximum supprime l'effet du hachage temporel.

Profil cumule, extrait autour du pic :

```
868.400  -90 dBm |
868.425  -38 dBm |######################################
868.450  -48 dBm |############################
868.475  -48 dBm |############################
868.500  -65 dBm |###########
868.550  -66 dBm |##########
```

Lobe unique. Les points a -51 et -62 dBm vers 869,000 et 869,125 MHz sont des
sources tierces du logement, pas la telecommande.

### Reserve sur cette mesure

Neuf points contigus juste sous le pic (868,200 a 868,400) affichent exactement
-90 dBm. Une valeur identique repetee ressemble a un plafonnement de lecture
plutot qu'a une mesure. Ce segment n'est pas exploite. Il n'affecte pas la
localisation du pic, qui est sans ambiguite.

## Modulation : question NON tranchee

Un FSK a forte deviation est **exclu** : la largeur occupee ne depasse pas la
largeur du filtre de mesure. S'il y a du FSK, la deviation est inferieure a
environ 25 kHz, ce que la resolution de 25 kHz ne sait pas separer.

Au-dela, rien n'est etabli. Deux tentatives de conclusion ont ete faites puis
retirees, elles sont consignees ici parce que les erreurs de methode sont
reutilisables :

**Tentative 1 : capture OOK a 868,35 MHz.** Sept bursts obtenus, tous du bruit
de slicer : RSSI au plancher, timings non quantifies de 50 a 7000 us. En OOK
sans porteuse, le slicer amplifie le bruit et sort des transitions aleatoires.

**Tentative 2 : critere d'enveloppe trop grossier.** Le RSSI a ete echantillonne
a 13,9 kHz pour repondre a « OOK ou FSK » : en OOK l'enveloppe est hachee au
rythme des bits, en FSK elle est constante. Le critere retenu, « dynamique
superieure a 10 dB donc OOK », ne regardait que l'amplitude et jamais le rythme.
Il a conclu a de l'OOK sur cinq captures qui n'etaient qu'une derive lente du
RSSI : `H223636 L144 H865 L64009`, soit quatre alternances sur 288 ms, la ou une
trame KEELOQ en compte plus de cent trente sur cinquante millisecondes.

**Correctif retenu** : une trame se reconnait au rythme, pas a l'amplitude. Le
detecteur exige desormais au moins 24 alternances dont 16 de duree comprise
entre 80 us et 3 ms. Trois captures ulterieures ont ete correctement rejetees.

## Contrainte de portee

| Distance | Puissance recue |
|---|---|
| 20-30 cm | -38 dBm |
| Autre etage, a travers un plancher | -61 a -70 dBm |

L'ecart de 25 a 30 dB correspond a la perte en espace libre attendue. Une
telecommande distante reste **detectable** mais son enveloppe est trop
comprimee pour etre binarisee : les creux ne redescendent plus au plancher.

Toute tentative de capture de trame doit donc se faire **telecommande a 20-30 cm
de la carte**. C'est une contrainte physique, aucun reglage ne la contourne.

Note utile pour la suite du projet : la detection a -70 dBm depuis un autre
etage indique qu'un seul emetteur bien place pourra vraisemblablement couvrir
plusieurs volets sans repeteur.

## Reglages CC1101 valides sur ce banc

| Registre | Valeur | Raison |
|---|---|---|
| `FREQ2/1/0` | `0x21 0x66 0xA5` | 868,425 MHz mesure |
| `AGCCTRL2` | `0x07` | gain maximal. `0x43` essaye : perte de sensibilite |
| `AGCCTRL1` | `0x00` | absent du depot, l'AGC par defaut est calibre pour du 2-FSK |
| `AGCCTRL0` | `0x91` | idem |
| `MCSM1` | `0x3C` | reste en RX. Sans lui la puce retombe en IDLE |

Contraintes de bus relevees sur ce cablage Dupont :

- SPI a 2 et 5 MHz : lectures a `0xFF` sur `VERSION` et `MARCSTATE`. **1 MHz** est fiable.
- Delai de garde autour de CS : **5 us**. A 4 us, toutes les lectures retombent a `0xFF`.
- Les registres de statut peuvent renvoyer une valeur corrompue pendant leur mise
  a jour. Relire jusqu'a obtenir deux lectures consecutives identiques.
- Le filtre anti-glitch RMT de l'ESP32 plafonne a **3187 ns** (8 bits d'horloge
  APB). Au-dela, `rmt_receive()` echoue avec `ESP_ERR_INVALID_ARG`.

## Etat

| Point | Statut |
|---|---|
| Cablage CC1101 | Valide, ecriture/relecture 4 motifs sur 4 |
| Telecommande emet | Confirme, +52 dB au-dessus du plancher |
| Frequence porteuse | **868,425 MHz**, deux methodes concordantes |
| FSK a forte deviation | Exclu |
| OOK ou FSK faible deviation | **Ouvert** |
| Trames capturees | **Aucune** |

Prochaine etape : capture en champ proche, telecommande a 20-30 cm.
