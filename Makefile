MINGW_STRIP = i686-w64-mingw32-strip
MINGW_CC = i686-w64-mingw32-gcc
GCC = gcc
CFLAGS = -Wall -std=c99 -Werror -I.
RELEASE_CFLAGS = $(CFLAGS) -Os
DEBUG_CFLAGS = $(CFLAGS) -g
TEST_SRC = tests.c CuTest.c
TEST_HDR = CuTest.h
ifeq ($(PREFIX),)
PREFIX=$(HOME)/echeck
endif

default: echeck

all: echeck.zip echeck

install: echeck
	install -t $(PREFIX) echeck
	install -d $(PREFIX)/e2/de
	install -d $(PREFIX)/e2/en
	install -d $(PREFIX)/e3/de
	install -d $(PREFIX)/e3/en
	install -t $(PREFIX)/e2/de e2/de/*.txt
	install -t $(PREFIX)/e2/en e2/en/*.txt
	install -t $(PREFIX)/e3/de e3/de/*.txt
	install -t $(PREFIX)/e3/en e3/en/*.txt

tags:
	@ctags *.c *.h

echeck.exe: echeck.c unicode.c unicode.h config.h
	$(MINGW_CC) $(RELEASE_CFLAGS) -o echeck.exe echeck.c unicode.c
	$(MINGW_STRIP) echeck.exe

echeck.dbg: echeck.c unicode.c unicode.h config.h $(TEST_SRC) $(TEST_HDR)
	$(GCC) $(DEBUG_CFLAGS) -DWITH_CUTEST -o echeck.dbg echeck.c unicode.c $(TEST_SRC)

echeck: echeck.c unicode.c unicode.h config.h
	$(GCC) $(RELEASE_CFLAGS) -o echeck echeck.c unicode.c

clean:
	@rm -f *.o core *.bak echeck echeck.dbg echeck.exe echeck.zip

test: echeck.dbg
	@./echeck.dbg -T=all -Lde -Re3 -b
	@./echeck.dbg -T=all -Lde -Re2 -b

echeck.zip: echeck.exe changelog.txt LIESMICH.txt README.txt
	zip -r echeck.zip echeck.exe e2 e3 changelog.txt LIESMICH.txt README.txt
