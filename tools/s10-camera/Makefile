CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?= -lm
DESTDIR ?=
PREFIX ?= /usr/local

all: s5k2l4-capture

s5k2l4-capture: s5k2l4-capture.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: s5k2l4-capture
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 s5k2l4-capture $(DESTDIR)$(PREFIX)/bin/
	install -m 755 s5k2l4-capture-onboot $(DESTDIR)$(PREFIX)/bin/
	install -d $(DESTDIR)/etc/systemd/system
	install -m 644 s5k2l4-capture.service $(DESTDIR)/etc/systemd/system/

clean:
	rm -f s5k2l4-capture

.PHONY: all install clean
