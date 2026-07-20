CXX ?= g++
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic

.PHONY: all clean windows

all: loopbackrec

loopbackrec: loopbackrec.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ -Wl,-l:libpulse-simple.so.0 -Wl,-l:libpulse.so.0

windows: loopbackrec.exe

loopbackrec.exe: loopbackrec.cpp
	x86_64-w64-mingw32-g++ $(CXXFLAGS) $< -o $@ -lole32 -luuid -static-libgcc -static-libstdc++

clean:
	$(RM) loopbackrec loopbackrec.exe
