# Thin convenience wrapper around the CMake build (see AGENTS.md §1 for details).
# JUCE is resolved by CMakeLists.txt (-DJUCE_PATH / $JUCE_PATH / ../JUCE / FetchContent),
# so no JUCE path needs to be passed here.
#
#   make            # configure + build (Release)
#   make build      # same as above
#   make clean      # remove the build directory
#   make rebuild    # clean, then build
#
# Overridable:  make BUILD_TYPE=Debug   |   make JOBS=8   |   make BUILD_DIR=out

BUILD_DIR  ?= build
BUILD_TYPE ?= Release
JOBS       ?= $(shell nproc 2>/dev/null || echo 4)

.PHONY: all build clean rebuild

all: build

build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR) -j$(JOBS)

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean build
