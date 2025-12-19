# Test Makefile for hed textobjects

CC = clang
CFLAGS = -Wall -Wextra -std=c23 -pedantic -g -O0
INCLUDES = -I../src -I../src/buf -I../src/lib
LDFLAGS =

# Source files needed for tests
TEXTOBJ_SRC = ../src/buf/textobj.c

# Test binaries
TEST_TEXTOBJ = test_textobj

.PHONY: all clean test

all: $(TEST_TEXTOBJ)

$(TEST_TEXTOBJ): test_textobj.c $(TEXTOBJ_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

test: $(TEST_TEXTOBJ)
	@echo "Running textobject tests..."
	@./$(TEST_TEXTOBJ)

clean:
	rm -f $(TEST_TEXTOBJ)
