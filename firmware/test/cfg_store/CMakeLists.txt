# Banc de test de cfg_store.c sur PC (cible linux d'ESP-IDF) : voir README.md.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)
# Comme le test hote officiel de nvs_flash : FreeRTOS remplace par sa maquette.
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/freertos/")
# AddressSanitizer : le modele possede de la memoire (listes des centrales) ; une
# erreur memoire arrete le banc au lieu de passer inapercue.
idf_build_set_property(COMPILE_OPTIONS "-fsanitize=address;-fno-omit-frame-pointer" APPEND)
idf_build_set_property(LINK_OPTIONS "-fsanitize=address" APPEND)
project(cfg_store_host_test)
