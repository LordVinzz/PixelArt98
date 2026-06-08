BUILD_DIR ?= build
BUILD_TYPE ?= Release
BACKEND ?= SDL2
CMAKE ?= cmake
CTEST ?= ctest

ifeq ($(OS),Windows_NT)
RUN_EXE := $(BUILD_DIR)/pixelart_sdl2.exe
else
RUN_EXE := ./$(BUILD_DIR)/pixelart_sdl2
endif

CMAKE_CONFIGURE_FLAGS ?= \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DPIXELART_BACKEND=$(BACKEND) \
	-DPIXELART_BUILD_BOTH=ON \
	-DPIXELART_BUILD_TESTS=ON

.PHONY: all configure build test run clean

all: configure build test

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) $(CMAKE_CONFIGURE_FLAGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --config $(BUILD_TYPE)

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --build-config $(BUILD_TYPE) --output-on-failure --timeout 60

run: build
	$(RUN_EXE)

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR)
