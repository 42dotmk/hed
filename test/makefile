# Test Makefile for hed textobjects

CC = clang
CFLAGS = -D_GNU_SOURCE -D_DEFAULT_SOURCE -Wall -Wextra -std=c23 -pedantic -g -O0
INCLUDES = -I../src -I../src/buf -I../src/lib -I../vendor -Iunity
LDFLAGS =

# Source files needed for tests
TEXTOBJ_SRC = ../src/buf/textobj.c ../src/buf/row.c ../src/lib/strutil.c \
              ../src/lib/strbuf.c
INPUT_SRC = ../src/input/input.c
REGISTERS_SRC = ../src/input/registers.c ../src/lib/strbuf.c
MAIL_CHAT_SRC = ../plugins/mail/mail_parse.c ../src/lib/strbuf.c \
                ../src/lib/strutil.c ../src/lib/stb_ds.c
UNITY_SRC = unity/unity.c
HELPER_SRC = test_helpers.c

# Test binaries
TEST_TEXTOBJ = test_textobj
TEST_INPUT = test_input
TEST_REGISTERS = test_registers
TEST_MAIL_CHAT = test_mail_chat

.PHONY: all clean test

all: $(TEST_TEXTOBJ) $(TEST_INPUT) $(TEST_REGISTERS) $(TEST_MAIL_CHAT)

$(TEST_TEXTOBJ): test_textobj.c $(TEXTOBJ_SRC) $(UNITY_SRC) $(HELPER_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

$(TEST_INPUT): test_input.c $(INPUT_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

$(TEST_REGISTERS): test_registers.c $(REGISTERS_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

$(TEST_MAIL_CHAT): test_mail_chat.c $(MAIL_CHAT_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

test: $(TEST_TEXTOBJ) $(TEST_INPUT) $(TEST_REGISTERS) $(TEST_MAIL_CHAT)
	@echo "Running textobject tests..."
	@./$(TEST_TEXTOBJ)
	@echo "Running input parser tests..."
	@./$(TEST_INPUT)
	@echo "Running register tests..."
	@./$(TEST_REGISTERS)
	@echo "Running mail chat-view tests..."
	@./$(TEST_MAIL_CHAT)

clean:
	rm -f $(TEST_TEXTOBJ) $(TEST_INPUT) $(TEST_REGISTERS) $(TEST_MAIL_CHAT)
