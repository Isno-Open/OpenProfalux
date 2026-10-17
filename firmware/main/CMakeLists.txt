# board_pins.h est ENGENDRE a la configuration depuis boards/${BOARD}.json, dans
# le dossier de construction : rien d'engendre n'est commis, et changer de carte
# c'est changer une variable, pas un fichier source. boards/ et tools/ vivent a
# la racine du depot, partages avec firmware-capture-test/.
#
# ESP-IDF lit ce fichier une premiere fois en « expansion precoce » dans un autre
# processus CMake : on n'engendre qu'a la vraie configuration.
set(BOARD_JSON "${CMAKE_CURRENT_LIST_DIR}/../../boards/${BOARD}.json")
set(BOARD_HDR_DIR "${CMAKE_BINARY_DIR}/board")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT EXISTS "${BOARD_JSON}")
        message(FATAL_ERROR "carte inconnue : ${BOARD_JSON} n'existe pas (-DBOARD=isno-super | external | d1-mini | m5-atom-lite)")
    endif()
    file(MAKE_DIRECTORY "${BOARD_HDR_DIR}")
    execute_process(COMMAND python3 "${CMAKE_CURRENT_LIST_DIR}/../../tools/gen_board.py" "${BOARD_JSON}" "${BOARD_HDR_DIR}/board_pins.h"
                    RESULT_VARIABLE r)
    if(NOT r EQUAL 0)
        message(FATAL_ERROR "gen_board.py a refuse ${BOARD_JSON}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BOARD_JSON}" "${CMAKE_CURRENT_LIST_DIR}/../../tools/gen_board.py")
endif()

# L'interface web est embarquee COMPRESSEE (gzip) : ~113 Ko de sources tiennent en ~35 Ko
# de flash, envoyes tels quels (Content-Encoding: gzip, voir send_asset). Les .gz sont
# engendres a la configuration dans le dossier de construction, comme board_pins.h ;
# modifier un fichier de web/ relance la configuration, donc la compression.
set(WEB_SRCS "${CMAKE_CURRENT_LIST_DIR}/web/index.html"
             "${CMAKE_CURRENT_LIST_DIR}/web/style.css"
             "${CMAKE_CURRENT_LIST_DIR}/web/app.js"
             "${CMAKE_CURRENT_LIST_DIR}/web/manifest.webmanifest")
set(WEB_GZ_DIR "${CMAKE_BINARY_DIR}/web")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    execute_process(COMMAND python3 "${CMAKE_CURRENT_LIST_DIR}/../../tools/gzip_web.py" "${WEB_GZ_DIR}" ${WEB_SRCS}
                    RESULT_VARIABLE r)
    if(NOT r EQUAL 0)
        message(FATAL_ERROR "gzip_web.py n'a pas pu compresser l'interface web")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${WEB_SRCS} "${CMAKE_CURRENT_LIST_DIR}/../../tools/gzip_web.py")
endif()

idf_component_register(
    SRCS
        "main.c"
        "cc1101.c"
        "wifi_bridge.c"
        "mqtt_bridge.c"
        "mqtt_tls_policy.c"
        "shutters.c"
        "cfg_store.c"
        "cfg_model.c"
        "keeloq.c"
        "keeloq_decode.c"
        "mdns_name.c"
        "captive_dns.c"
        "enrol_gate.c"
        "freq_sweep.c"
        "pfx_keys.c"
        "pfx_enrol.c"
        "web_ui.c"
        "ota.c"
        "radio.c"
        "radio_profile.c"
        "log_ring.c"
    INCLUDE_DIRS "." "${BOARD_HDR_DIR}"
    EMBED_FILES
        "${WEB_GZ_DIR}/index.html.gz"
        "${WEB_GZ_DIR}/style.css.gz"
        "${WEB_GZ_DIR}/app.js.gz"
        "${WEB_GZ_DIR}/manifest.webmanifest.gz"
        "web/icon-192.png"
    REQUIRES
        nvs_flash
        spiffs
        esp_wifi
        esp_netif
        esp_eth
        esp_event
        mqtt
        espressif__cjson
        driver
        esp_driver_spi
        esp_driver_gpio
        esp_driver_rmt
        esp_http_server
        app_update
        esp_https_ota
        esp_http_client
        esp-tls
        mbedtls
)
