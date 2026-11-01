CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
PKG_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 gio-2.0)
PKG_LIBS := $(shell pkg-config --libs gtk+-3.0 gio-2.0) -lpthread -lm

all: thinkpad-lights thinkpad-lights-gui

thinkpad-lights: src/common.h src/leds.c src/pattern.c src/main.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ src/leds.c src/pattern.c src/main.c $(PKG_LIBS)

thinkpad-lights-gui: src/common.h src/leds.c src/pattern.c src/gui.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ src/leds.c src/pattern.c src/gui.c $(PKG_LIBS)

clean:
	rm -f thinkpad-lights thinkpad-lights-gui

install: all
	install -d $(HOME)/.local/share/thinkpad-lights
	install -m 755 thinkpad-lights thinkpad-lights-gui $(HOME)/.local/share/thinkpad-lights/
	install -d $(HOME)/.local/bin
	ln -sf $(HOME)/.local/share/thinkpad-lights/thinkpad-lights $(HOME)/.local/bin/thinkpad-lights
	ln -sf $(HOME)/.local/share/thinkpad-lights/thinkpad-lights-gui $(HOME)/.local/bin/thinkpad-lights-gui