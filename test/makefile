# Test Makefile for hed textobjects

CC = clang
CFLAGS = -D_GNU_SOURCE -D_DEFAULT_SOURCE -Wall -Wextra -std=c23 -pedantic -g -O0
INCLUDES = -I../src -I../src/buf -I../src/lib -I../vendor -Iunity
LDFLAGS =

# Source files needed for tests
TEXTOBJ_SRC = ../src/buf/textobj.c
INPUT_SRC = ../src/input/input.c
UNITY_SRC = unity/unity.c
HELPER_SRC = test_helpers.c

# Test binaries
TEST_TEXTOBJ = test_textobj
TEST_INPUT = test_input

.PHONY: all clean test

all: $(TEST_TEXTOBJ) $(TEST_INPUT)

$(TEST_TEXTOBJ): test_textobj.c $(TEXTOBJ_SRC) $(UNITY_SRC) $(HELPER_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

$(TEST_INPUT): test_input.c $(INPUT_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

test: $(TEST_TEXTOBJ) $(TEST_INPUT)
	@echo "Running textobject tests..."
	@./$(TEST_TEXTOBJ)
	@echo "Running input parser tests..."
	@./$(TEST_INPUT)

clean:
	rm -f $(TEST_TEXTOBJ) $(TEST_INPUT)
