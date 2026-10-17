# Banc de test de freq_sweep (porteuse par balayage RSSI), sur PC. Voir README.md.
#
# freq_sweep.c est PUR (stdint/string, aucune dependance ESP-IDF) : un simple
# gcc/clang suffit, et AddressSanitizer arrete le banc a la moindre erreur memoire.
cmake_minimum_required(VERSION 3.16)
project(freq_sweep_host_test C)

add_executable(freq_sweep_host_test
    main/test_main.c
    ../../main/freq_sweep.c)
target_include_directories(freq_sweep_host_test PRIVATE main ../../main)
target_compile_options(freq_sweep_host_test PRIVATE
    -std=gnu11 -Wall -Wextra -Wno-unused-parameter
    -fsanitize=address -fno-omit-frame-pointer -g)
target_link_options(freq_sweep_host_test PRIVATE -fsanitize=address)
