# Firmware capture / test (banc)

Firmware ESP-IDF (M5Stack ATOM Lite + CC1101) utilisé pour l'**analyse au banc** du
moteur Profalux 868 MHz : capture de trames RF, rejeu (RollJam), et tests de sécurité.
Ce n'est **pas** le firmware de production Home Assistant (à venir dans `../firmware/`).

Voir l'analyse : `../../OpenProfalux-Research/analysis/SECURITE-MOTEUR-REJEU.md`.

## Bouton ATOM (GPIO39) : comptage d'appuis rapides (fenêtre 700 ms)

| Appuis | Action |
|---|---|
| **1** | ENREGISTRE une séquence (écoute continue, dédoublonne, timing réel) |
| **2** | Rejeu **brut** de la séquence (magnétophone) |
| **3** | Rejeu **via OpenProfalux** (`pfx_frame_build_with_hop`, preuve d'émission) |
| **4** | RECHARGE les trames depuis NVS (rejeu après coupure de courant) |
| **5** | TEST clair/hop (référence EMPX + bouton clair dérivé 0x1/0x2/0x4) |
| **6+** | Efface le journal NVS |

Chaque trame captée est persistée en **NVS** (survit à une coupure, redump au boot).

## Ce que ce firmware a établi

- La capture RX marche (CC1101 sur GDO0, boucle anti-bruit type sniffer).
- Le moteur se pilote **par rejeu, sans clé** : anti-rejeu cassé (compteur ignoré).
- L'anti-falsification est actif (bouton clair == bouton déchiffré) → une trame par commande.

## Build / flash

ESP-IDF v5.2.2, cible ESP32 (`m5stack_atom`).

```
idf.py build
idf.py -p <PORT> flash monitor
```

Sous WSL sans usbipd : builder ici, copier `build/*.bin` côté Windows, flasher avec
`esptool.exe` sur le bon COM (voir la mémoire projet pour la procédure exacte).
