CXX = clang++ -std=c++20 -D_GNU_SOURCE -O3

INCLUDE_ALL = -Iinclude
CFLAGS = -Wall -Wextra -fstrict-aliasing -Wstrict-aliasing=2 $(INCLUDE_ALL) \
	 -Wconversion -Wsign-conversion -Wpedantic -Wno-gnu-zero-variadic-macro-arguments -lglog
CFLAGS_DEBUG = -ggdb -fsanitize=address,undefined
TEST_LIB = -lgtest

BUILDDIR = build
TESTSDIR = build/tests

OBJ_C = $(CC) $(INCLUDE_ALL) $(CFLAGS) -c
OBJ_CXX = $(CXX) $(INCLUDE_ALL) $(CFLAGS) -c
BIN_C = $(CC) $(INCLUDE_ALL) $(CFLAGS)
BIN_CXX = $(CXX) $(INCLUDE_ALL) $(CFLAGS)

FILE_LIST = src/main.cc src/server.cc src/irc.cc src/loop.cc

default: setup kbot

setup:
	mkdir -p build
	mkdir -p build/tests

kbot: setup
	$(BIN_CXX) $(CFLAGS_DEBUG) -o build/kbot $(FILE_LIST)

kbot_test: setup kbot
	$(BIN_CXX) $(CFLAGS_DEBUG) $(TEST_LIB) -o $(TESTSDIR)/test_irc_message src/tests/test_irc_message.cc

all_tests_run: setup kbot kbot_test
	./$(TESTSDIR)/test_irc_message

clean:
	rm -rf build/
