# cfg_model.c et cfg_store.c sont compiles TELS QUELS depuis le firmware : ce sont
# eux qu'on teste.
idf_component_register(SRCS "test_main.c"
                            "../../../main/cfg_model.c"
                            "../../../main/cfg_store.c"
                       INCLUDE_DIRS "." "../../../main"
                       REQUIRES nvs_flash esp_partition json)
# Coupure de courant : nos fonctions s'intercalent entre la NVS et la flash emulee.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_partition_write_raw"
    "-Wl,--wrap=esp_partition_write"
    "-Wl,--wrap=esp_partition_erase_range")
