# Trames d'une télécommande KeeLoq : répétitions et bits d'état

Relevé le 2026-10-10 au YARD Stick One (démodulation OOK à 10 kéch/s, décodage PWM des
durées d'impulsion) sur une télécommande murale Eveno (HCS30x), à côté du récepteur.

## Ce que fait la télécommande

| | Mesure |
|---|---|
| Trame | préambule, en-tête (~4,45 ms de silence), 66 bits PWM (1 = haut court, bas long) |
| Ordre des bits | code tournant (32), numéro de série (28), boutons (4), bit 64, bit 65 |
| Répétitions | la trame est répétée tant que le bouton est enfoncé, **même code tournant** |
| Silence entre deux trames | ~18 ms (17,8 à 18,3 mesurés) |
| Bit 64 | 0 sur toutes les trames (indicateur de pile faible : piles bonnes) |
| Bit 65 | **0 sur la première trame d'un appui, 1 sur toutes les répétitions** |

Le bit 65 est le bit de répétition (RPT). Sur 6 appuis analysés, les trois premières trames
identifiées avec certitude (aucun signal capté avant) ont RPT = 0 ; les ~16 répétitions
identifiées ont toutes RPT = 1.

## Ce que fait le boîtier

Par défaut, une commande est une rafale de `PRESS_REPEATS` (10) trames **identiques**, à
2 ms d'intervalle : le bit 65 est celui de la trame enregistrée (0 pour une trame adoptée
depuis le journal, dont les bits d'état sont reconstruits à 0). Les moteurs Eveno Zuni-R
testés obéissent ainsi.

L'option **« Émettre comme la télécommande »** (Système > Appareil, ou
`"tx_repeat_bit": true` dans `POST /api/config`) force RPT = 0 sur la première trame de la
rafale, RPT = 1 sur les suivantes, et espace les trames de 18 ms. Elle vise les moteurs qui
tiendraient compte de ce bit, et un maintien fidèle (appui long). Désactivée par défaut.
