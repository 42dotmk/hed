CC=LD_LIBRARY_PATH=/usr/lib clang
BASE_CFLAGS = $(shell cat compile_flags.txt | tr '\n' ' ')
CFLAGS = $(BASE_CFLAGS)

TS_LDFLAGS := -ltree-sitter -ldl

SRC_DIR = src
BUILD_DIR = build

TARGET = $(BUILD_DIR)/hed
TSI    = $(BUILD_DIR)/tsi
SOURCES = $(shell find $(SRC_DIR) -type f -name "*.c")
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))


.PHONY: all clean run test_args ts-langs strip_build

all: clean strip_build

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS) 	
	$(CC) -s -o $@ $^ $(LDFLAGS) $(TS_LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	# echo '@':$@, '^':$^, '<':$<
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TSI): ts/ts_lang_install.c
	$(CC) -std=c23 -Wall -Wextra -O2 -I/usr/include -o $@ $<

ts-langs: $(BUILD_DIR)
	@echo "Tree-sitter source files:"
	cp -rf ts-langs build/ts-langs

strip_build: $(BUILD_DIR) $(TARGET) $(TSI) ts-langs
	@# Keep only final binaries in $(BUILD_DIR)
	@find $(BUILD_DIR) -mindepth 1 -maxdepth 1 ! -name 'hed' ! -name 'tsi' -exec rm -rf {} +

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)
