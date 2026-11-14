# Compiler et flasher OpenProfalux depuis un Mac (ESP-IDF natif)

Le build via Docker évite d'installer ESP-IDF, mais sur Mac le passage de l'USB à travers Docker est
capricieux au moment de flasher. Cette procédure installe **ESP-IDF v6.1 en natif**, où l'USB est géré
directement. Elle est donnée pour un **M5Stack ATOM Lite** (ESP32 classique) ; pour une autre carte,
adapter la cible (`set-target`) et le `-DBOARD` comme dans [`../firmware/README.md`](../firmware/README.md).

> Procédure partagée et testée par **@jean-paulgabert** (4 volets Profalux Visio de 2014), dans la
> discussion [« Ça marche chez vous ? » #116](https://github.com/Isno-Open/OpenProfalux/discussions/116).

Prérequis : [Homebrew](https://brew.sh) installé.

## 1. Les prérequis

Système de compilation, Python, flash USB, git :

```bash
brew install cmake ninja dfu-util python3 git
```

## 2. ESP-IDF v6.1 et ses sous-modules

Le clone est long et semble se figer par moments, c'est normal.

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v6.1 --recursive https://github.com/espressif/esp-idf.git
```

## 3. La toolchain ESP32 et ESP32-S3

Se termine sur `All done! You can now run: . ./export.sh`.

```bash
cd ~/esp/esp-idf
./install.sh esp32,esp32s3
```

## 4. Charger l'environnement

À refaire dans **chaque nouveau Terminal** (le point initial exécute le script dans le shell courant,
pour que `idf.py` soit trouvé) :

```bash
. ~/esp/esp-idf/export.sh
```

> **Le piège Python.** macOS fournit Python 3.9, et ESP-IDF v6.1 exige 3.10 ou plus. Si cette étape
> s'arrête sur `Python 3.10+ is not installed`, installer une version récente, puis relancer l'étape 3 :
>
> ```bash
> brew install python@3.12
> cd ~/esp/esp-idf && ./install.sh esp32,esp32s3
> ```

## 5. OpenProfalux : configuration et compilation

Le M5 ATOM Lite est un ESP32 classique. `-DBOARD` donne le brochage de la carte, `-B` un dossier de
build dédié. La compilation est longue et se termine sur `Project build complete`.

```bash
cd ~/esp
git clone https://github.com/Isno-Open/OpenProfalux.git
cd OpenProfalux/firmware
idf.py -B build-m5-atom-lite -DBOARD=m5-atom-lite set-target esp32
idf.py -B build-m5-atom-lite -DBOARD=m5-atom-lite build
```

## 6. Flasher et surveiller la console

Brancher le M5 ATOM Lite en USB-C, repérer son port, puis flasher :

```bash
ls /dev/cu.*                       # repérer le port, du type cu.usbserial-5Dxxxx
idf.py -B build-m5-atom-lite -p /dev/cu.usbserial-5Dxxxx flash monitor
```

Dans la console qui défile, trois lignes confirment que tout est bon :

- `OpenProfalux v... target=m5-atom-lite` : le firmware a démarré ;
- `PARTNUM=0x00 VERSION=0x14 ... MARCSTATE=...` : le CC1101 répond, le câblage SPI est correct ;
- `TX SELFTEST OK` : le CC1101 émet.

Le module est prêt. La suite (Wi-Fi, apprentissage d'une télécommande, intégration Home Assistant) est
dans [`../firmware/README.md`](../firmware/README.md).
