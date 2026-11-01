CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
PKG_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 gio-2.0)
PKG_LIBS := $(shell pkg-config --libs gtk+-3.0 gio-2.0) -lpthread -lm

all: blinkpad blinkpad-gui

blinkpad: src/common.h src/leds.c src/pattern.c src/main.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ src/leds.c src/pattern.c src/main.c $(PKG_LIBS)

blinkpad-gui: src/common.h src/leds.c src/pattern.c src/gui.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ src/leds.c src/pattern.c src/gui.c $(PKG_LIBS)

clean:
	rm -f blinkpad blinkpad-gui

install: all
	install -d $(HOME)/.local/share/blinkpad
	install -m 755 blinkpad blinkpad-gui $(HOME)/.local/share/blinkpad/
	install -d $(HOME)/.local/bin
	ln -sf $(HOME)/.local/share/blinkpad/blinkpad $(HOME)/.local/bin/blinkpad
	ln -sf $(HOME)/.local/share/blinkpad/blinkpad-gui $(HOME)/.local/bin/blinkpad-gui