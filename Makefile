CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= /usr/local

INCLUDES = -Ivendor/nvidia/sdk -Ivendor/nvidia/unix

spectractl: src/spectractl.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $<

install: spectractl
	install -Dm755 spectractl $(DESTDIR)$(PREFIX)/bin/spectractl

clean:
	rm -f spectractl

.PHONY: install clean
