CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

TARGET := build/MiniFileExplorer
SOURCES := src/main.cpp
OBJECTS := $(SOURCES:src/%.cpp=build/%.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	@if [ -d build ]; then rm -r build; fi
