CC=LD_LIBRARY_PATH=/usr/lib gcc
BASE_CFLAGS = $(shell cat compile_flags.txt | tr '\n' ' ')
CFLAGS = $(BASE_CFLAGS)

SRC_DIR = src
BUILD_DIR = build

# PLUGINS_DIR: where plugins live. Override on the command line to use a
# different plugin set, e.g.:
#     make PLUGINS_DIR=$HOME/my-hed-plugins
# Plugins from this directory are compiled and -I'd. The default is the
# in-tree plugins/.
PLUGINS_DIR ?= plugins

# WITH_TREESITTER: 1 builds the treesitter plugin and links libtree-sitter,
# 0 excludes both. Core uses weak refs to the ts_* symbols, so the editor
# builds and runs cleanly either way (no syntax highlighting when off).
#     make WITH_TREESITTER=0
WITH_TREESITTER ?= 1

TARGET = $(BUILD_DIR)/hed
TSI    = $(BUILD_DIR)/tsi

# Core sources: everything under src/ except the in-tree plugins subtree.
CORE_SOURCES = $(shell find $(SRC_DIR) -type f -name "*.c" -not -path "$(SRC_DIR)/plugins/*")
PLUGIN_SOURCES = $(shell find $(PLUGINS_DIR) -type f -name "*.c" 2>/dev/null)

# Optional plugin filtering / library linking.
ifeq ($(WITH_TREESITTER),1)
TS_LDFLAGS := -ltree-sitter -ldl
else
PLUGIN_SOURCES := $(filter-out $(PLUGINS_DIR)/treesitter/%,$(PLUGIN_SOURCES))
TS_LDFLAGS := -ldl
endif

SOURCES = $(CORE_SOURCES) $(PLUGIN_SOURCES)
HEADERS = $(shell find $(SRC_DIR) -type f -name "*.h") \
          $(shell find $(PLUGINS_DIR) -type f -name "*.h" 2>/dev/null)

CORE_OBJECTS   = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CORE_SOURCES))
PLUGIN_OBJECTS = $(patsubst $(PLUGINS_DIR)/%.c,$(BUILD_DIR)/plugins/%.o,$(PLUGIN_SOURCES))
OBJECTS = $(CORE_OBJECTS) $(PLUGIN_OBJECTS)


.PHONY: all clean run test test_args ts-langs strip_build

all: $(TARGET) $(TSI)

test:
	$(MAKE) -C test test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS) $(TS_LDFLAGS)

# Compile rule for plugins (PLUGINS_DIR-rooted). Listed first so that when
# PLUGINS_DIR == src/plugins the longer prefix wins over the generic rule.
$(BUILD_DIR)/plugins/%.o: $(PLUGINS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(PLUGINS_DIR) -c $< -o $@

# Compile rule for core sources.
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(PLUGINS_DIR) -c $< -o $@

$(TSI): ts/ts_lang_install.c
	$(CC) -Wall -Wextra -O2 -I/usr/include -o $@ $<

publish:
	@echo "Publishing hed and tsi to /usr/local/bin/"
	$(MAKE) install

install: $(TARGET) $(TSI)
	cp $(TARGET) /usr/local/bin/hed
	cp $(TSI) /usr/local/bin/tsi

ts-langs: $(BUILD_DIR)
	@echo "Tree-sitter source files:"
	cp -rf ts-langs build/ts-langs
	cp -rf ts-langs/* ~/.config/hed/ts/

strip_build: $(BUILD_DIR) $(TARGET) $(TSI) ts-langs
	@# Keep only final binaries in $(BUILD_DIR)
	@find $(BUILD_DIR) -mindepth 1 -maxdepth 1 ! -name 'hed' ! -name 'tsi' -exec rm -rf {} +

clean:
	rm -rf $(BUILD_DIR)

fmt:
	clang-format -i $(SOURCES) $(HEADERS)

tags:
	ctags -R --languages=C src $(PLUGINS_DIR)


run: $(TARGET)
	./$(TARGET)
