OS_NAME := $(shell uname -s | tr A-Z a-z)
MINGW_STRIP = i686-w64-mingw32-strip
MINGW_CC = i686-w64-mingw32-gcc
CFLAGS = -Wall -std=c99 -I. -DHAVE_GETTEXT -D_DEFAULT_SOURCE
RELEASE_CFLAGS = -Os -Werror
DEBUG_CFLAGS = -g
TEST_SRC = tests.c CuTest.c
TEST_HDR = CuTest.h
PREFIX ?= /usr
SHAREDIR ?= $(PREFIX)/share/games/echeck
EXECDIR ?= $(PREFIX)/games
LOCALEDIR ?= $(PREFIX)/share/locale
default: echeck mofiles
ifeq ($(OS_NAME),darwin)
LFLAGS += -lintl
endif	

all: echeck mofiles

pofiles: po/echeck.de.po 

mofiles: pofiles locale/de/LC_MESSAGES/echeck.mo

install: echeck mofiles
	install -d $(EXECDIR)
	install -C -t $(EXECDIR) echeck
	install -d $(SHAREDIR)/e2/de
	install -d $(SHAREDIR)/e2/en
	install -d $(SHAREDIR)/e3/de
	install -d $(SHAREDIR)/e3/en
	install -C -m 644 -t $(SHAREDIR)/e2/de e2/de/*.txt
	install -C -m 644 -t $(SHAREDIR)/e2/en e2/en/*.txt
	install -C -m 644 -t $(SHAREDIR)/e3/de e3/de/*.txt
	install -C -m 644 -t $(SHAREDIR)/e3/en e3/en/*.txt
	install -C -m 644 -t $(LOCALEDIR)/de/LC_MESSAGES locale/de/LC_MESSAGES/echeck.mo

tags:
	@ctags *.c *.h

po/echeck.pot: echeck.c
	xgettext -k_ -kt -d echeck -o $@ -s $^

po/echeck.%.po: po/echeck.pot
	msgmerge -N -o $@ $@ $< 

locale/%/LC_MESSAGES/echeck.mo: po/echeck.de.po 
	@rm -rf $@
	@mkdir -p $@
	@rmdir $@
	msgfmt -f -c $< -o $@

echeck: main.c echeck.c unicode.c unicode.h whereami.c whereami.h
	$(CC) $(LFLAGS) $(CFLAGS) $(RELEASE_FLAGS) -o echeck main.c echeck.c unicode.c whereami.c

tests: echeck.c whereami.c whereami.h unicode.c unicode.h $(TEST_SRC) $(TEST_HDR)
	$(CC) $(LFLAGS) $(CFLAGS) $(DEBUG_FLAGS) -DTESTING -o tests echeck.c whereami.c unicode.c $(TEST_SRC)

clean:
	@rm -f *.o core *.bak tests echeck echeck.exe echeck.zip 
	@rm -rf locale

check: tests
	@./tests -T=all -Lde -Re2 -b
