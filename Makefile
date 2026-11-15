CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra

all: bin/tests bin/bench bin/hull_cli

bin:
	mkdir -p bin

bin/tests: cpp/tests.cpp cpp/*.hpp | bin
	$(CXX) $(CXXFLAGS) $< -o $@

bin/bench: cpp/bench.cpp cpp/*.hpp | bin
	$(CXX) $(CXXFLAGS) $< -o $@

bin/hull_cli: cpp/hull_cli.cpp cpp/*.hpp | bin
	$(CXX) $(CXXFLAGS) $< -o $@

test: bin/tests
	./bin/tests

clean:
	rm -rf bin

.PHONY: all test clean
