# BALLAST - build with `make`, run with `make run`
CC      ?= cc
CFLAGS  ?= -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
CFLAGS  += $(shell pkg-config --cflags sdl3)
LDLIBS  += $(shell pkg-config --libs sdl3) -lm
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o)
BIN     := ballast

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c src/common.h src/game_internal.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

run: $(BIN)
	./$(BIN)

clean:
	rm -rf build $(BIN)

.PHONY: all run clean
