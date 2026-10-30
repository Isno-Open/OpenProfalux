# Journal des defauts : session alpha du 2026-08-10

Banc de test : ESP32-PICO-D4 (rev v1.1, MAC `c8:85:41:4d:cc:18`), flash 4 Mo,
relie a un adaptateur FTDI FT232R sur COM4. Cablage CC1101 selon la table
**M5Stack ATOM Lite** de [WIRING.md](WIRING.md). Build ESP-IDF v5.2.2 sous WSL2,
flash et console serie depuis Windows (WSL2 n'a pas de passthrough USB ici).

## Etat du materiel : valide

Sonde SPI dediee, executee avant toute autre chose :

```
CC1101 PARTNUM = 0x00        conforme
CC1101 VERSION = 0x04        revision de silicium ancienne, courante sur les E07
ecriture/relecture 0xAA, 0x55, 0x3C, 0xC3 sur SYNC1 et PKTLEN : 4/4 OK
GDO0 (g25) bascule entre deux lectures
```

Les quatre motifs relus a l'identique prouvent que MISO, MOSI, SCK et CSN sont
bons et non inverses. Une ligne MISO en l'air ne peut pas restituer ce qu'on lui
ecrit, contrairement au seul PARTNUM (voir defaut n7).

Le basculement de GDO0 est un effet de bord utile : apres un `SRES`, `IOCFG0`
reprend sa valeur par defaut `0x3F`, soit « sortie horloge XOSC/192 ». Le pin qui
bouge prouve donc que **le quartz 26 MHz du module oscille** et que l'alimentation
3,3 V est correcte.

## Defauts bloquants pour le build : corriges

### 1. Aucun `Kconfig` ne definissait les cibles

`hardware_config.h:71` fait `#error` si ni `CONFIG_OPENPROFALUX_TARGET_EXTERNAL`
ni `CONFIG_OPENPROFALUX_TARGET_M5STACK` n'est defini. Or aucun fichier `Kconfig`
ni `Kconfig.projbuild` n'existait dans le depot. Le `CONFIG_OPENPROFALUX_TARGET_EXTERNAL=y`
pose dans `sdkconfig.defaults` portait donc sur un symbole inconnu : kconfgen
l'ignore silencieusement et il n'atterrit jamais dans `sdkconfig.h`.

**Correction** : ajout de `firmware/main/Kconfig.projbuild` avec un `choice`
entre les deux cibles.

### 2. `hardware_config.h` n'incluait pas `sdkconfig.h`

Meme une fois le `Kconfig` en place, le `#error` continuait de partir sur
`profalux.c` et `mqtt_bridge.c`. ESP-IDF ne force pas l'inclusion de `sdkconfig.h` :
tout fichier qui inclut `hardware_config.h` **avant** un en-tete IDF ne voit aucun
symbole `CONFIG_*`. `main.c` passait par chance, les deux autres non.

**Correction** : `#include "sdkconfig.h"` en tete de `hardware_config.h`.

### 3. `mqtt_bridge.c` utilisait `TARGET_NAME` sans l'include

`mqtt_bridge.c:130` concatene `TARGET_NAME` dans le payload JSON de
`mqtt_pub_system_status()`, mais le fichier n'incluait pas `hardware_config.h`.
Erreur `expected ')' before 'TARGET_NAME'`.

**Correction** : ajout de l'include.

### 4. `-Werror=format` sur les `uint32_t`

Sur ESP-IDF 5.x, `uint32_t` est un `long unsigned int`. Les `%u` et `%08X`
appliques directement a des `uint32_t` sont donc des erreurs, pas des
avertissements. Six sites concernes :

| Fichier | Ligne | Champ |
|---|---|---|
| `profalux.c` | 23 | `st->serial`, `st->counter` |
| `profalux.c` | 108 | `n_frames`, `duration_ms` |
| `profalux.c` | 117 | `st->counter` |
| `mqtt_bridge.c` | 116 | `frames` |
| `main.c` | 55 | `g_state.serial`, `g_state.counter` |
| `main.c` | 97, 103 | `rxf.serial`, `rxf.encrypted_hop` |

**Correction** : cast en `(unsigned)` sur chaque site. L'alternative propre est
`PRIu32` / `PRIX32` de `<inttypes.h>`, plus verbeuse.

### 5. Taille de flash insuffisante pour `partitions.csv`

`partitions.csv` declare `factory` 1536K + `storage` 512K, soit 2,1 Mo, alors que
la taille de flash par defaut d'ESP-IDF est 2 Mo. La generation de la table de
partitions echoue avant meme la compilation.

**Correction** : `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` dans `sdkconfig.defaults`.
La puce du banc a bien 4 Mo.

## Defaut bloquant pour la capture : non corrige

### 6. La reception n'est pas implementee

Dans `cc1101.c:187`, `rx_task()` est un squelette :

```c
static void rx_task(void *pv) {
    strobe(CC_SRX);
    while (s_rx_running) {
        /* TODO: Sample GDO0 with hardware timer + detect preamble + decode bits.
         * This skeleton just polls RSSI and provides infrastructure. */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ...
}
```

Le callback `s_rx_cb` n'est jamais appele, GDO0 n'est jamais echantillonne.
`mqtt_pub_rx_frame()` et le topic `openprofalux/listen/frame` ne peuvent donc
rien publier. En l'etat, le firmware capture zero trame.

S'ajoute que `listen/start` n'arrive que par MQTT : il faut WiFi configure et un
broker joignable avant d'esperer declencher quoi que ce soit. Pour de l'alpha,
c'est trois dependances de trop entre l'antenne et l'ecran.

**Contournement retenu** : un sniffer autonome separe, qui echantillonne GDO0 via
le peripherique RMT de l'ESP32 a 1 us pres et sort les timings bruts sur UART,
sans WiFi ni MQTT. Voir [SNIFFER.md](SNIFFER.md).

## Fragilites du driver CC1101 : a corriger

### 7. `cc1101_init()` valide la puce sur le seul PARTNUM

`cc1101.c:143` ne teste que `partnum == 0x00`. Or **une ligne MISO non connectee
lit elle aussi 0x00** : le test passe alors qu'il n'y a aucune puce au bout du
bus. C'est un faux positif silencieux, exactement le cas qu'on cherche a
detecter.

**Recommandation** : tester `VERSION` (0x31) en plus, et surtout faire une
ecriture/relecture sur un registre de config. Voir la sonde de cette session.

### 8. Frequence porteuse calee 50 kHz trop bas

`cc1101.c` pose `FREQ2/1/0 = 0x21/0x65/0x6A`, soit `0x21656A` = 2 188 650.

```
f = 2188650 x 26e6 / 2^16 = 868,300 MHz
```

La cible documentee partout ailleurs dans le depot est **868,35 MHz**, ce qui
demande `0x2165E8` (`FREQ0 = 0xE8`). L'ecart est de 50 kHz pour une bande passante
RX de 58 kHz (`MDMCFG4 = 0xF6`) : le signal tombe en bord de filtre. Tolerable en
emission, penalisant en reception.

### 9. Aucun registre AGC configure

La table `s_regs` ne contient ni `AGCCTRL2` (0x1B), ni `AGCCTRL1` (0x1C), ni
`AGCCTRL0` (0x1D). Ils gardent donc leurs valeurs de reset, calibrees pour du
2-FSK, pas pour de l'OOK. En ASK/OOK le controle de gain doit etre borne
explicitement, sinon l'AGC pompe sur le bruit entre les impulsions et la
reception devient sourde.

Valeurs utilisees par le sniffer : `AGCCTRL2 = 0x07`, `AGCCTRL1 = 0x00`,
`AGCCTRL0 = 0x91`.

### 10. `MCSM1` absent : la puce quitte le mode RX

Sans `MCSM1`, `RXOFF_MODE` vaut 0 et le CC1101 retombe en IDLE des la fin d'une
reception. Il faut le remettre en RX a la main entre chaque trame. `MCSM1 = 0x3C`
le fait rester en RX.

## Documentation a jour

### 11. `BUILD.md` decrit un environnement qui n'existe pas ici

- Chemin `cd /home/olivier/projects/openprofalux` : le depot est ailleurs.
- `idf.py -p /dev/ttyUSB0 flash monitor` : sous WSL2 sans `usbipd`, aucun
  `/dev/ttyUSB*` n'existe. Le flash et la console passent par Windows.

Voir la section ajoutee dans [BUILD.md](../BUILD.md).
