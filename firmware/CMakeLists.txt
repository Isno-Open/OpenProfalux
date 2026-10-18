cmake_minimum_required(VERSION 3.16)
# Version affichee au boot, dans l'UI (header) et en MQTT. A BUMPER a chaque release.
# Alimente esp_app_get_description()->version (app descriptor).
set(PROJECT_VER "0.2.11")
# La carte visee : -DBOARD=<nom d'un fichier de boards/>, ex. isno-super (ESP32-S3) ou
# m5-atom-lite (ESP32). Chacune a sa cible IDF (champ mcu), voir BUILD.md. board_pins.h en
# est engendre a la config.
set(BOARD "isno-super" CACHE STRING "carte visee : boards/<BOARD>.json")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Versions des composants du registre (mdns, mqtt, cjson, lan87xx) figees dans un fichier
# COMMIS par puce : deux builds du meme commit embarquent les memes composants. Un seul
# fichier ne suffit pas : lan87xx n'existe que pour esp32, et chaque changement de puce le
# recalculerait. Monter une version : effacer le fichier de la puce, `idf.py -B build-<carte>
# reconfigure` (un simple build ne le recree pas ; update-dependencies ne voit pas ce nom),
# commettre le fichier.
idf_build_set_property(DEPENDENCIES_LOCK dependencies.lock.${IDF_TARGET})
project(openprofalux)
