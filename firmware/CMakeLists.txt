cmake_minimum_required(VERSION 3.16)
# Version affichee au boot, dans l'UI (header) et en MQTT. A BUMPER a chaque release.
# Alimente esp_app_get_description()->version (app descriptor).
set(PROJECT_VER "0.2.15")
# Build de test : la variable d'environnement OPF_VERSION (ex. "0.2.12-test9") surcharge la
# version sans toucher ce fichier, pour que le testeur VOIE dans l'UI qu'il est bien sur la
# build de test (sinon tout s'annonce 0.2.11 et personne ne peut verifier son flash). Les
# releases ne definissent pas OPF_VERSION et gardent la valeur ci-dessus. Voir build-test.sh.
if(DEFINED ENV{OPF_VERSION} AND NOT "$ENV{OPF_VERSION}" STREQUAL "")
  set(PROJECT_VER "$ENV{OPF_VERSION}")
endif()
# La carte visee : -DBOARD=<nom d'un fichier de boards/>, ex. isno-super (ESP32-S3) ou
# m5-atom-lite (ESP32). Chacune a sa cible IDF (champ mcu), voir BUILD.md. board_pins.h en
# est engendre a la config.
set(BOARD "isno-super" CACHE STRING "carte visee : boards/<BOARD>.json")
# Ce que la memoire de la carte decide (budgets reseau) : un fragment sdkconfig engendre depuis
# sa declaration (champ psram, voir tools/gen_board.py), insere juste apres sdkconfig.defaults.
# Il doit exister avant project(), qui lit les valeurs par defaut ; une carte inconnue est
# refusee plus tard par main/CMakeLists.txt, avec la liste des cartes.
set(BOARD_SDKCONFIG "${CMAKE_BINARY_DIR}/board.sdkconfig")
# Meme carte que main/CMakeLists.txt : BOARD_FILE (carte hors de boards/, banc QEMU) l'emporte.
if(DEFINED BOARD_FILE AND NOT "${BOARD_FILE}" STREQUAL "")
  set(BOARD_DECL "${BOARD_FILE}")
else()
  set(BOARD_DECL "${CMAKE_CURRENT_LIST_DIR}/../boards/${BOARD}.json")
endif()
if(EXISTS "${BOARD_DECL}")
  execute_process(COMMAND python3 "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_board.py" --sdkconfig
                          "${BOARD_DECL}" "${BOARD_SDKCONFIG}"
                  RESULT_VARIABLE board_sdk_rc)
  if(NOT board_sdk_rc EQUAL 0)
    message(FATAL_ERROR "gen_board.py --sdkconfig a refuse ${BOARD_DECL}")
  endif()
  if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
  endif()
  list(INSERT SDKCONFIG_DEFAULTS 1 "${BOARD_SDKCONFIG}")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Versions des composants du registre (mdns, mqtt, cjson, lan87xx) figees dans un fichier
# COMMIS par puce : deux builds du meme commit embarquent les memes composants. Un seul
# fichier ne suffit pas : lan87xx n'existe que pour esp32, et chaque changement de puce le
# recalculerait. Monter une version : effacer le fichier de la puce, `idf.py -B build-<carte>
# reconfigure` (un simple build ne le recree pas ; update-dependencies ne voit pas ce nom),
# commettre le fichier.
idf_build_set_property(DEPENDENCIES_LOCK dependencies.lock.${IDF_TARGET})
project(openprofalux)
