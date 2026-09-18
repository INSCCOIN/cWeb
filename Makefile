CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= /usr/local

cWeb: cWeb.c html.c html.h
	$(CC) $(CFLAGS) -o cWeb cWeb.c html.c -lncurses

install: cWeb
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 cWeb $(DESTDIR)$(PREFIX)/bin/cWeb

clean:
	rm -f cWeb
