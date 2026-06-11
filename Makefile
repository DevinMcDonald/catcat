CMAKE ?= cmake
BUILD_DIR ?= build
BUILD_TYPE ?= Release

.PHONY: all build package clean test

all: build

$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: $(BUILD_DIR)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR)

package: $(BUILD_DIR)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR) --target package_catcat

test:
	build/catcat_tests

clean:
	@echo "Cleaning up $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)
