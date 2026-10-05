# Banc de test du decodeur OOK HCS30x/KeeLoq, sur PC. Voir README.md.
#
# keeloq_decode.c est PUR (stdint/string, aucune dependance ESP-IDF), donc ce
# banc se compile avec un simple gcc/clang : pas d'IDF, pas de docker, il tourne
# partout et en CI en quelques secondes. AddressSanitizer arrete le banc sur la
# moindre erreur memoire plutot que de la laisser passer.
cmake_minimum_required(VERSION 3.16)
project(keeloq_host_test C)

add_executable(keeloq_host_test
    main/test_main.c
    ../../main/keeloq_decode.c)
target_include_directories(keeloq_host_test PRIVATE main ../../main)
target_compile_options(keeloq_host_test PRIVATE
    -std=gnu11 -Wall -Wextra -Wno-unused-parameter
    -fsanitize=address -fno-omit-frame-pointer -g)
target_link_options(keeloq_host_test PRIVATE -fsanitize=address)
