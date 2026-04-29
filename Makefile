# Makefile for KVStore – In-Memory Key-Value Store
# Works with g++ (MinGW / Linux / macOS)

CXX       := g++
CXXFLAGS  := -std=c++17 -O2 -Wall -Wextra -Wpedantic
SRCDIR    := src
SOURCES   := $(SRCDIR)/main.cpp $(SRCDIR)/kvstore.cpp $(SRCDIR)/command_handler.cpp $(SRCDIR)/server.cpp
TARGET    := kvstore

# Platform detection
ifeq ($(OS),Windows_NT)
    TARGET  := kvstore.exe
    LDFLAGS := -lws2_32 -lpthread
else
    LDFLAGS := -lpthread
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) dump.json
