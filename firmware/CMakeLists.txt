cmake_minimum_required(VERSION 3.16)
# Version affichee au boot, dans l'UI (header) et en MQTT. A BUMPER a chaque release.
# Alimente esp_app_get_description()->version (app descriptor).
set(PROJECT_VER "0.2.3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(openprofalux)
