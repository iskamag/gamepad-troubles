CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
TTF_LIBS   := $(shell pkg-config --libs SDL2_ttf 2>/dev/null)

BINS := sdl2-gamepad-test sdl2-gamepad-gui evdev-gamepad-test

.PHONY: all clean
all: $(BINS)

sdl2-gamepad-test: sdl2-gamepad-test.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) -lm

sdl2-gamepad-gui: sdl2-gamepad-gui.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) $(TTF_LIBS) -lm

evdev-gamepad-test: evdev-gamepad-test.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(BINS)
