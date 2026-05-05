CC=LD_LIBRARY_PATH=/usr/lib gcc
BASE_CFLAGS = $(shell cat compile_flags.txt | tr '\n' ' ')
CFLAGS = $(BASE_CFLAGS) -MMD -MP

SRC_DIR = src
BUILD_DIR = build
PLUGINS_DIR ?= plugins
WITH_TREESITTER ?= 1

TS_DIR     := vendor/tree-sitter
TS_LIB_DIR := $(TS_DIR)/lib
TS_LIB_A   := $(BUILD_DIR)/libtree-sitter.a

TARGET = $(BUILD_DIR)/hed
TSI    = $(BUILD_DIR)/tsi

INSTALL_DIR ?= $(HOME)/.local/bin

# Core sources: everything under src/ except the in-tree plugins subtree.
CORE_SOURCES = $(shell find $(SRC_DIR) -type f -name "*.c" -not -path "$(SRC_DIR)/plugins/*")
PLUGIN_SOURCES = $(shell find $(PLUGINS_DIR) -type f -name "*.c" 2>/dev/null)

ifeq ($(WITH_TREESITTER),1)
TS_LDFLAGS  := $(TS_LIB_A) -ldl -rdynamic
TS_DEPS     := $(TS_LIB_A)
else
PLUGIN_SOURCES := $(filter-out $(PLUGINS_DIR)/treesitter/%,$(PLUGIN_SOURCES))
TS_LDFLAGS  := -ldl
TS_DEPS     :=
endif

SOURCES = $(CORE_SOURCES) $(PLUGIN_SOURCES)
HEADERS = $(shell find $(SRC_DIR) -type f -name "*.h") \
          $(shell find $(PLUGINS_DIR) -type f -name "*.h" 2>/dev/null)

CORE_OBJECTS   = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CORE_SOURCES))
PLUGIN_OBJECTS = $(patsubst $(PLUGINS_DIR)/%.c,$(BUILD_DIR)/plugins/%.o,$(PLUGIN_SOURCES))
OBJECTS = $(CORE_OBJECTS) $(PLUGIN_OBJECTS)


.PHONY: all clean run test test_args ts-langs strip_build install uninstall publish fmt tags

all: $(TARGET) $(TSI)

test:
	$(MAKE) -C test test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS) $(TS_DEPS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS) $(TS_LDFLAGS)

# Pull in auto-generated header dependencies (created by -MMD).
-include $(OBJECTS:.o=.d)


$(TS_LIB_A): $(TS_LIB_DIR)/src/lib.c | $(BUILD_DIR)
	$(CC) -O2 -std=c11 -fPIC -D_GNU_SOURCE \
	    -I$(TS_LIB_DIR)/include -I$(TS_LIB_DIR)/src \
	    -c $< -o $(BUILD_DIR)/ts-lib.o
	$(AR) rcs $@ $(BUILD_DIR)/ts-lib.o


$(BUILD_DIR)/plugins/%.o: $(PLUGINS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(PLUGINS_DIR) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(PLUGINS_DIR) -c $< -o $@

$(TSI): ts/ts_lang_install.c
	$(CC) -Wall -Wextra -O2 -I/usr/include -o $@ $<


install: $(TARGET) $(TSI)
	@mkdir -p $(INSTALL_DIR)
	ln -sf $(abspath $(TARGET)) $(INSTALL_DIR)/hed
	ln -sf $(abspath $(TSI))    $(INSTALL_DIR)/tsi
	@echo "Symlinked hed and tsi -> $(INSTALL_DIR)"

publish: $(TARGET) $(TSI)
	@echo "Publishing hed and tsi to /usr/local/bin/"
	cp $(TARGET) /usr/local/bin/hed
	cp $(TSI) /usr/local/bin/tsi

uninstall:
	rm -f $(INSTALL_DIR)/hed $(INSTALL_DIR)/tsi
	@echo "Removed hed and tsi symlinks from $(INSTALL_DIR)"

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
	ctags -R --languages=C $(SRC_DIR) $(PLUGINS_DIR)

run: $(TARGET)
	./$(TARGET)
