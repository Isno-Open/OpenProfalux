# Banc de test de enrol_gate (apprentissage apres capture reussie seulement), sur PC.
# enrol_gate.c est PUR : un simple gcc/clang suffit. Voir README.md.
cmake_minimum_required(VERSION 3.16)
project(enrol_gate_host_test C)

add_executable(enrol_gate_host_test
    main/test_main.c
    ../../main/enrol_gate.c)
target_include_directories(enrol_gate_host_test PRIVATE main ../../main)
target_compile_options(enrol_gate_host_test PRIVATE
    -std=gnu11 -Wall -Wextra -Wno-unused-parameter
    -fsanitize=address -fno-omit-frame-pointer -g)
target_link_options(enrol_gate_host_test PRIVATE -fsanitize=address)
