CXX ?= g++
STEMIL_ROOT ?= ../third_party/stemil
CPPFLAGS := -Isrc -I$(STEMIL_ROOT)/src
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -Wpedantic
# The original stemil implementation owns arrays larger than 2 GiB in total.
MODELFLAGS := -mcmodel=medium
UPSTREAM := $(STEMIL_ROOT)/src/coordinates.cpp $(STEMIL_ROOT)/src/moves.cpp $(STEMIL_ROOT)/src/moving_tables.cpp $(STEMIL_ROOT)/src/pruning_tables.cpp $(STEMIL_ROOT)/src/search.cpp
CORE := src/cube.cpp src/tables.cpp src/repair.cpp src/solver.cpp src/shuffle.cpp

.PHONY: all test test-cli sanitize clean
all: build/stemil-rephase
build:
	mkdir -p build
build/stemil-rephase: $(CORE) src/main.cpp $(UPSTREAM) $(wildcard src/*.hpp) Makefile | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MODELFLAGS) $(CORE) src/main.cpp $(UPSTREAM) -o $@
build/tests: $(CORE) tests/test.cpp $(UPSTREAM) $(wildcard src/*.hpp) Makefile | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MODELFLAGS) $(CORE) tests/test.cpp $(UPSTREAM) -o $@
test: build/tests
	./build/tests
test-cli: build/stemil-rephase
	python3 tests/cli_test.py
sanitize: | build
	$(CXX) $(CPPFLAGS) -std=c++17 -O1 -g $(MODELFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(CORE) tests/test.cpp $(UPSTREAM) -o build/tests-sanitize
	./build/tests-sanitize
clean:
	$(RM) build/stemil-rephase build/tests build/tests-sanitize
