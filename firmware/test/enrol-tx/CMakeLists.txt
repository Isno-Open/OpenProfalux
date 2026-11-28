# Banc de test de enrol_tx (reglages d'emission de la session d'enrolement), sur PC.
# enrol_tx.c est PUR : un simple gcc/clang suffit. Voir README.md.
cmake_minimum_required(VERSION 3.16)
project(enrol_tx_host_test C)

add_executable(enrol_tx_host_test
    main/test_main.c
    ../../main/enrol_tx.c)
target_include_directories(enrol_tx_host_test PRIVATE main ../../main)
target_compile_options(enrol_tx_host_test PRIVATE
    -std=gnu11 -Wall -Wextra -Wno-unused-parameter
    -fsanitize=address -fno-omit-frame-pointer -g)
target_link_options(enrol_tx_host_test PRIVATE -fsanitize=address)
