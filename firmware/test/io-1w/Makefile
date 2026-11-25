CXX ?= c++
CC ?= cc
IO = ../../main/io
BUILD ?= ../../build-host-io-1w
FLAGS = -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer -Istubs -I$(IO)

.PHONY: test
test:
	mkdir -p $(BUILD)
	$(CC) -std=c11 $(FLAGS) -c $(IO)/io_store.c -o $(BUILD)/io_store.o
	$(CXX) -std=c++17 $(FLAGS) test_main.cpp $(IO)/iohome_frame.cpp $(IO)/io_send.cpp $(IO)/io_pair.cpp $(BUILD)/io_store.o -o $(BUILD)/test_io
	$(BUILD)/test_io
