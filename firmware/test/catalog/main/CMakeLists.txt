# catalog.c est compile TEL QUEL depuis le firmware : c'est lui qu'on teste.
idf_component_register(SRCS "test_main.c" "../../../main/catalog.c"
                       INCLUDE_DIRS "." "../../../main"
                       REQUIRES espressif__cjson)
