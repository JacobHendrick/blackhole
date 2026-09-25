CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
GL_FLAGS := $(shell pkg-config --cflags --libs glfw3 glew)

.PHONY: all run clean
all: blackhole

blackhole: BlackHolesun.c++ Renderer.hpp
	$(CXX) $(CXXFLAGS) -x c++ BlackHolesun.c++ -o $@ $(GL_FLAGS) -lGL

run: blackhole
	./blackhole

clean:
	rm -f blackhole
