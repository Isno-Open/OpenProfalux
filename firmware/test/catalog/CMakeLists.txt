# Banc de test de catalog.c sur PC (cible linux d'ESP-IDF) : voir README.md.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/freertos/")
# AddressSanitizer : une erreur memoire dans la lecture du JSON arrete le banc.
idf_build_set_property(COMPILE_OPTIONS "-fsanitize=address;-fno-omit-frame-pointer" APPEND)
idf_build_set_property(LINK_OPTIONS "-fsanitize=address" APPEND)
project(catalog_host_test)
