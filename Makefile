# FuzzyFind - find anything on the Amiga instantly (README.md).
# Cross-built with Bebbo's m68k-amigaos-gcc. The matching and the index are
# in fuzzy.library (AmigaLibs, beside this repo); fzf draws with tui.library.
#
#   make        build/updatedb, build/locate, build/fzf (68000 code: the
#               work is in the libraries, which come in 000/020/060 builds)

AMIGA   ?= $(firstword $(wildcard /opt/amiga $(HOME)/opt/amiga))
CC      = $(AMIGA)/bin/m68k-amigaos-gcc
FUZZY   ?= ../AmigaLibs/fuzzy
TUI     ?= ../AmigaLibs/tui
CFLAGS  = -Os -m68000 -noixemul -fomit-frame-pointer -fno-common -fbbb=- -Wall -Wno-pointer-sign \
          -I$(FUZZY)/include -I$(TUI)/include -Isrc

CMDS = updatedb locate fzf

all: $(CMDS:%=build/%)

build/updatedb build/locate: build/%: src/%.c src/version.h $(FUZZY)/include/libraries/fuzzy.h
	@mkdir -p build
	$(CC) $(CFLAGS) -s -o $@ $<

build/fzf: src/fzf.c src/keys.c src/fzf.h src/version.h $(FUZZY)/include/libraries/fuzzy.h $(TUI)/include/libraries/tui.h
	@mkdir -p build
	$(CC) $(CFLAGS) -s -o $@ src/fzf.c src/keys.c

clean:
	rm -rf build

.PHONY: all clean
