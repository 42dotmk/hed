CC=LD_LIBRARY_PATH=/usr/lib clang
BASE_CFLAGS = $(shell cat compile_flags.txt | tr '\n' ' ')

VTERM_CFLAGS := $(shell echo "$(BASE_CFLAGS)" | grep -q "USE_LIBVTERM" && pkg-config --cflags vterm 2>/dev/null || echo "")

CFLAGS = $(BASE_CFLAGS) $(VTERM_CFLAGS)

LDFLAGS =

# Auto-add Tree-sitter link flags if USE_TREESITTER present in CFLAGS

TS_LDFLAGS := $(shell echo "$(CFLAGS)" | grep -q "USE_TREESITTER" && echo "-ltree-sitter -ldl" || echo "")
VTERM_LDFLAGS := $(shell echo "$(CFLAGS)" | grep -q "USE_LIBVTERM" && pkg-config --libs vterm 2>/dev/null || echo "")

SRC_DIR = src
BUILD_DIR = build

TARGET = $(BUILD_DIR)/hed
SOURCES = $(shell find $(SRC_DIR) -type f -name "*.c")
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

.PHONY: all clean run test_args

all: clean $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS) $(TS_LDFLAGS) $(VTERM_LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	# echo '@':$@, '^':$^, '<':$<
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)
