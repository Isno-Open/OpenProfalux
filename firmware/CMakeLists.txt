cmake_minimum_required(VERSION 3.16)
# Version affichee au boot, dans l'UI (header) et en MQTT. A BUMPER a chaque release.
# Alimente esp_app_get_description()->version (app descriptor).
set(PROJECT_VER "0.2.9")
# La carte visee : -DBOARD=isno-super (ESP32-S3), external ou m5-atom-lite (ESP32).
# Chacune a sa cible IDF, voir README. board_pins.h en est engendre a la config.
set(BOARD "isno-super" CACHE STRING "carte visee : boards/<BOARD>.json")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(openprofalux)
