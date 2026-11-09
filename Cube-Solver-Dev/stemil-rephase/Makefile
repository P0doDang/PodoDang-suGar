CXX ?= g++
STEMIL_ROOT ?= ../third_party/stemil
CPPFLAGS := -Isrc -I$(STEMIL_ROOT)/src
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -Wpedantic -pthread
# The original stemil implementation owns arrays larger than 2 GiB in total.
MODELFLAGS := -mcmodel=medium
UPSTREAM := $(STEMIL_ROOT)/src/coordinates.cpp $(STEMIL_ROOT)/src/moves.cpp $(STEMIL_ROOT)/src/moving_tables.cpp $(STEMIL_ROOT)/src/pruning_tables.cpp $(STEMIL_ROOT)/src/search.cpp
CORE := src/cube.cpp src/tables.cpp src/repair.cpp src/solver.cpp src/shuffle.cpp src/symmetry.cpp src/kernel.cpp src/optimal.cpp

.PHONY: all test test-cli test-optimal sanitize clean
all: build/stemil-rephase build/optimal build/stemil-orig build/optimal-batch
build:
	mkdir -p build
build/stemil-rephase: $(CORE) src/main.cpp $(UPSTREAM) $(wildcard src/*.hpp) Makefile | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MODELFLAGS) $(CORE) src/main.cpp $(UPSTREAM) -o $@
build/optimal: $(CORE) tools/optimal_main.cpp $(UPSTREAM) $(wildcard src/*.hpp) Makefile | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MODELFLAGS) $(CORE) tools/optimal_main.cpp $(UPSTREAM) -o $@
build/tests: $(CORE) tests/test.cpp $(UPSTREAM) $(wildcard src/*.hpp) Makefile | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MODELFLAGS) $(CORE) tests/test.cpp $(UPSTREAM) -o $@
test: build/tests
	./build/tests
build/stemil-orig: src/cube.cpp compare/stemil_orig.cpp $(UPSTREAM) $(wildcard src/*.hpp) Makefile | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MODELFLAGS) src/cube.cpp compare/stemil_orig.cpp $(UPSTREAM) -o $@
build/optimal-batch: $(CORE) compare/optimal_batch.cpp $(UPSTREAM) $(wildcard src/*.hpp) Makefile | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MODELFLAGS) $(CORE) compare/optimal_batch.cpp $(UPSTREAM) -o $@
test-optimal: build/optimal
	./build/optimal selftest
test-cli: build/stemil-rephase
	python3 tests/cli_test.py
sanitize: | build
	$(CXX) $(CPPFLAGS) -std=c++17 -O1 -g $(MODELFLAGS) -pthread -fsanitize=address,undefined -fno-omit-frame-pointer $(CORE) tests/test.cpp $(UPSTREAM) -o build/tests-sanitize
	./build/tests-sanitize
clean:
	$(RM) build/stemil-rephase build/optimal build/tests build/tests-sanitize
