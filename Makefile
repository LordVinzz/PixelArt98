BUILD_DIR ?= build
BUILD_TYPE ?= Release
BACKEND ?= SDL2
CMAKE ?= cmake
CTEST ?= ctest
DEPS_DIR ?= .deps
OPENCV_VERSION ?= 4.13.0
OPENCV_SOURCE_DIR ?= $(DEPS_DIR)/opencv-$(OPENCV_VERSION)
OPENCV_BUILD_DIR ?= $(DEPS_DIR)/opencv-$(OPENCV_VERSION)-build
OPENCV_PREFIX ?= $(abspath $(DEPS_DIR)/opencv-$(OPENCV_VERSION)-install)
OPENCV_DIR ?= $(OPENCV_PREFIX)/lib/cmake/opencv4
OPENCV_STAMP ?= $(OPENCV_PREFIX)/.pixelart-opencv-$(OPENCV_VERSION)-$(BUILD_TYPE)-shared-dnn-v2.stamp
ONNXRUNTIME_VERSION ?= 1.25.1
ONNXRUNTIME_PREFIX ?= $(abspath $(DEPS_DIR)/onnxruntime-$(ONNXRUNTIME_VERSION))
ONNXRUNTIME_SOURCE_DIR ?= $(DEPS_DIR)/onnxruntime-src-$(ONNXRUNTIME_VERSION)
ONNXRUNTIME_STAMP ?= $(ONNXRUNTIME_PREFIX)/.pixelart-onnxruntime-$(ONNXRUNTIME_VERSION).stamp

ifeq ($(OS),Windows_NT)
RUN_EXE := $(BUILD_DIR)/pixelart_sdl2.exe
ONNXRUNTIME_PLATFORM := win-x64
ONNXRUNTIME_ARCHIVE_EXT := zip
ONNXRUNTIME_TAR_FLAGS := xf
ONNXRUNTIME_EXTRACTED_DIR := onnxruntime-$(ONNXRUNTIME_PLATFORM)-$(ONNXRUNTIME_VERSION)
else
RUN_EXE := ./$(BUILD_DIR)/pixelart_sdl2
UNAME_S := $(shell uname -s 2>/dev/null || echo unknown)
UNAME_M := $(shell uname -m 2>/dev/null || echo unknown)
ifeq ($(UNAME_S),Darwin)
ifeq ($(UNAME_M),arm64)
ONNXRUNTIME_PLATFORM := osx-arm64
else
ONNXRUNTIME_PLATFORM := osx-x86_64
endif
else
ONNXRUNTIME_PLATFORM := linux-x64
endif
ONNXRUNTIME_ARCHIVE_EXT := tgz
ONNXRUNTIME_TAR_FLAGS := xzf
ONNXRUNTIME_EXTRACTED_DIR := onnxruntime-$(ONNXRUNTIME_PLATFORM)-$(ONNXRUNTIME_VERSION)
endif
ONNXRUNTIME_ARCHIVE := $(DEPS_DIR)/$(ONNXRUNTIME_EXTRACTED_DIR).$(ONNXRUNTIME_ARCHIVE_EXT)
ONNXRUNTIME_URL := https://github.com/microsoft/onnxruntime/releases/download/v$(ONNXRUNTIME_VERSION)/$(ONNXRUNTIME_EXTRACTED_DIR).$(ONNXRUNTIME_ARCHIVE_EXT)

CMAKE_CONFIGURE_FLAGS ?= \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DPIXELART_BACKEND=$(BACKEND) \
	-DPIXELART_BUILD_BOTH=ON \
	-DPIXELART_BUILD_TESTS=ON

DEPTH_CMAKE_CONFIGURE_FLAGS ?= \
	$(CMAKE_CONFIGURE_FLAGS) \
	-DPIXELART_ENABLE_ONNXRUNTIME=ON \
	-DPIXELART_ENABLE_OPENCV_DNN=OFF \
	-DONNXRUNTIME_ROOT=$(ONNXRUNTIME_PREFIX)

DEPTH_OPENCV_CMAKE_CONFIGURE_FLAGS ?= \
	$(CMAKE_CONFIGURE_FLAGS) \
	-DPIXELART_ENABLE_ONNXRUNTIME=OFF \
	-DPIXELART_ENABLE_OPENCV_DNN=ON \
	-DOpenCV_DIR=$(OPENCV_DIR)

DEPTH_ONNXRUNTIME_CMAKE_CONFIGURE_FLAGS ?= \
	$(CMAKE_CONFIGURE_FLAGS) \
	-DPIXELART_ENABLE_ONNXRUNTIME=ON \
	-DPIXELART_ENABLE_OPENCV_DNN=OFF \
	-DONNXRUNTIME_ROOT=$(ONNXRUNTIME_PREFIX)

DEPTH_ALL_CMAKE_CONFIGURE_FLAGS ?= \
	$(CMAKE_CONFIGURE_FLAGS) \
	-DPIXELART_ENABLE_ONNXRUNTIME=ON \
	-DPIXELART_ENABLE_OPENCV_DNN=ON \
	-DOpenCV_DIR=$(OPENCV_DIR) \
	-DONNXRUNTIME_ROOT=$(ONNXRUNTIME_PREFIX)

.PHONY: all configure configure-depth configure-depth-opencv configure-depth-onnxruntime configure-depth-all build build-depth build-depth-opencv build-depth-onnxruntime build-depth-all test test-depth test-depth-opencv test-depth-onnxruntime test-depth-all run clean clean-deps clean-opencv clean-onnxruntime deps-opencv deps-onnxruntime deps-onnxruntime-source depth-opencv depth-opencv-only depth-onnxruntime depth-backends print-depth-backends

all: configure build test

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) $(CMAKE_CONFIGURE_FLAGS)

configure-depth:
	$(CMAKE) -S . -B $(BUILD_DIR) $(DEPTH_CMAKE_CONFIGURE_FLAGS)

configure-depth-opencv:
	$(CMAKE) -S . -B $(BUILD_DIR) $(DEPTH_OPENCV_CMAKE_CONFIGURE_FLAGS)

configure-depth-onnxruntime:
	$(CMAKE) -S . -B $(BUILD_DIR) $(DEPTH_ONNXRUNTIME_CMAKE_CONFIGURE_FLAGS)

configure-depth-all:
	$(CMAKE) -S . -B $(BUILD_DIR) $(DEPTH_ALL_CMAKE_CONFIGURE_FLAGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --config $(BUILD_TYPE)

build-depth: configure-depth
	$(CMAKE) --build $(BUILD_DIR) --config $(BUILD_TYPE)

build-depth-opencv: configure-depth-opencv
	$(CMAKE) --build $(BUILD_DIR) --config $(BUILD_TYPE)

build-depth-onnxruntime: configure-depth-onnxruntime
	$(CMAKE) --build $(BUILD_DIR) --config $(BUILD_TYPE)

build-depth-all: configure-depth-all
	$(CMAKE) --build $(BUILD_DIR) --config $(BUILD_TYPE)

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --build-config $(BUILD_TYPE) --output-on-failure --timeout 60

test-depth: build-depth
	$(CTEST) --test-dir $(BUILD_DIR) --build-config $(BUILD_TYPE) --output-on-failure --timeout 60

test-depth-opencv: build-depth-opencv
	$(CTEST) --test-dir $(BUILD_DIR) --build-config $(BUILD_TYPE) --output-on-failure --timeout 60

test-depth-onnxruntime: build-depth-onnxruntime
	$(CTEST) --test-dir $(BUILD_DIR) --build-config $(BUILD_TYPE) --output-on-failure --timeout 60

test-depth-all: build-depth-all
	$(CTEST) --test-dir $(BUILD_DIR) --build-config $(BUILD_TYPE) --output-on-failure --timeout 60

run: build
	$(RUN_EXE)

deps-opencv: $(OPENCV_STAMP)

$(OPENCV_STAMP):
	$(CMAKE) -E make_directory $(DEPS_DIR)
	test -d $(OPENCV_SOURCE_DIR)/.git || git clone --depth 1 --branch $(OPENCV_VERSION) https://github.com/opencv/opencv.git $(OPENCV_SOURCE_DIR)
	$(CMAKE) -E rm -rf $(OPENCV_BUILD_DIR)
	$(CMAKE) -E rm -rf $(OPENCV_PREFIX)
	$(CMAKE) -S $(OPENCV_SOURCE_DIR) -B $(OPENCV_BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_INSTALL_PREFIX=$(OPENCV_PREFIX) \
		-DBUILD_LIST=core,imgproc,dnn \
		-DBUILD_SHARED_LIBS=ON \
		-DBUILD_TESTS=OFF \
		-DBUILD_PERF_TESTS=OFF \
		-DBUILD_EXAMPLES=OFF \
		-DBUILD_opencv_apps=OFF \
		-DBUILD_JAVA=OFF \
		-DBUILD_PYTHON=OFF \
		-DBUILD_TIFF=OFF \
		-DWITH_FFMPEG=OFF \
		-DWITH_GSTREAMER=OFF \
		-DWITH_IMGCODEC_HDR=OFF \
		-DWITH_IMGCODEC_PFM=OFF \
		-DWITH_IMGCODEC_PXM=OFF \
		-DWITH_JPEG=OFF \
		-DWITH_QT=OFF \
		-DWITH_OPENEXR=OFF \
		-DWITH_PNG=OFF \
		-DWITH_TIFF=OFF \
		-DWITH_WEBP=OFF \
		-DWITH_OPENCL=ON
	$(CMAKE) --build $(OPENCV_BUILD_DIR) --config $(BUILD_TYPE) --target install --parallel
	$(CMAKE) -E touch $(OPENCV_STAMP)

deps-onnxruntime: $(ONNXRUNTIME_STAMP)

$(ONNXRUNTIME_STAMP):
	$(CMAKE) -E make_directory $(DEPS_DIR)
	test -f $(ONNXRUNTIME_ARCHIVE) || curl -L --fail -o $(ONNXRUNTIME_ARCHIVE) $(ONNXRUNTIME_URL)
	$(CMAKE) -E rm -rf $(ONNXRUNTIME_PREFIX)
	$(CMAKE) -E rm -rf $(DEPS_DIR)/$(ONNXRUNTIME_EXTRACTED_DIR)
	$(CMAKE) -E chdir $(DEPS_DIR) $(CMAKE) -E tar $(ONNXRUNTIME_TAR_FLAGS) $(notdir $(ONNXRUNTIME_ARCHIVE))
	$(CMAKE) -E rename $(DEPS_DIR)/$(ONNXRUNTIME_EXTRACTED_DIR) $(ONNXRUNTIME_PREFIX)
	$(CMAKE) -E touch $(ONNXRUNTIME_STAMP)

deps-onnxruntime-source:
	$(CMAKE) -E make_directory $(DEPS_DIR)
	test -d $(ONNXRUNTIME_SOURCE_DIR)/.git || git clone --recursive --branch v$(ONNXRUNTIME_VERSION) https://github.com/microsoft/onnxruntime.git $(ONNXRUNTIME_SOURCE_DIR)
	cd $(ONNXRUNTIME_SOURCE_DIR) && git submodule update --init --recursive
	cd $(ONNXRUNTIME_SOURCE_DIR) && ./build.sh --config $(BUILD_TYPE) --build_shared_lib --parallel --skip_tests --compile_no_warning_as_error

depth-opencv: deps-opencv deps-onnxruntime test-depth-all

depth-opencv-only: deps-opencv test-depth-opencv

depth-onnxruntime: deps-onnxruntime test-depth-onnxruntime

depth-backends: deps-opencv deps-onnxruntime test-depth-all

print-depth-backends:
	@echo "OpenCV source:        $(OPENCV_SOURCE_DIR)"
	@echo "OpenCV install:       $(OPENCV_PREFIX)"
	@echo "OpenCV_DIR:           $(OPENCV_DIR)"
	@echo "ONNX Runtime archive: $(ONNXRUNTIME_URL)"
	@echo "ONNX Runtime root:    $(ONNXRUNTIME_PREFIX)"

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR)

clean-deps:
	$(CMAKE) -E rm -rf $(DEPS_DIR)

clean-opencv:
	$(CMAKE) -E rm -rf $(OPENCV_BUILD_DIR) $(OPENCV_PREFIX)

clean-onnxruntime:
	$(CMAKE) -E rm -rf $(ONNXRUNTIME_PREFIX)
