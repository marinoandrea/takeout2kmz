CC = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic

prefix = /usr/local
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
INSTALL = install
INSTALL_PROGRAM = $(INSTALL) -m 755

.PHONY: all clean install uninstall
all: saved_places_to_kmz

saved_places_to_kmz: main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(LDFLAGS) $(LDLIBS) -o $@

install: saved_places_to_kmz
	mkdir -p "$(DESTDIR)$(bindir)"
	$(INSTALL_PROGRAM) saved_places_to_kmz "$(DESTDIR)$(bindir)/saved_places_to_kmz"

uninstall:
	rm -f "$(DESTDIR)$(bindir)/saved_places_to_kmz"

clean:
	rm -f saved_places_to_kmz
