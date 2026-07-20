CXX ?= g++
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
VERSION := $(shell cat VERSION)
CPPFLAGS += -DREC_VERSION=\"$(VERSION)\"

.PHONY: all clean windows

all: rec

rec: rec.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ -Wl,-l:libpulse-simple.so.0 -Wl,-l:libpulse.so.0

windows: rec.exe

rec.exe: rec.cpp
	x86_64-w64-mingw32-g++ $(CPPFLAGS) $(CXXFLAGS) $< -o $@ -lole32 -luuid -static-libgcc -static-libstdc++

clean:
	$(RM) rec rec.exe
