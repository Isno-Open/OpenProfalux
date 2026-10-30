# Banc de test de rx_confirm (confirmation d'une trame recue), sur PC. Voir README.md.
#
# rx_confirm.c est PUR (stdint/string, aucune dependance ESP-IDF) : un simple
# gcc/clang suffit, et AddressSanitizer arrete le banc a la moindre erreur memoire.
cmake_minimum_required(VERSION 3.16)
project(rx_confirm_host_test C)

add_executable(rx_confirm_host_test
    main/test_main.c
    ../../main/rx_confirm.c)
target_include_directories(rx_confirm_host_test PRIVATE main ../../main)
target_compile_options(rx_confirm_host_test PRIVATE
    -std=gnu11 -Wall -Wextra -Wno-unused-parameter
    -fsanitize=address -fno-omit-frame-pointer -g)
target_link_options(rx_confirm_host_test PRIVATE -fsanitize=address)
