# Banc de test du codec logique Somfy RTS (somfy_rts.c), sur PC. Voir README.md.
#
# somfy_rts.c est PUR (stdint/string, aucune dependance ESP-IDF) : un simple
# gcc/clang suffit, et AddressSanitizer arrete le banc a la moindre erreur memoire.
cmake_minimum_required(VERSION 3.16)
project(somfy_rts_host_test C)

add_executable(somfy_rts_host_test
    main/test_main.c
    ../../main/somfy_rts.c)
target_include_directories(somfy_rts_host_test PRIVATE main ../../main)
target_compile_options(somfy_rts_host_test PRIVATE
    -std=gnu11 -Wall -Wextra -Wno-unused-parameter
    -fsanitize=address -fno-omit-frame-pointer -g)
target_link_options(somfy_rts_host_test PRIVATE -fsanitize=address)
