CC = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic

prefix = /usr/local
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
INSTALL = install
INSTALL_PROGRAM = $(INSTALL) -m 755

.PHONY: all clean install uninstall
all: takeout2kmz

takeout2kmz: main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(LDFLAGS) $(LDLIBS) -o $@

install: takeout2kmz
	mkdir -p "$(DESTDIR)$(bindir)"
	$(INSTALL_PROGRAM) takeout2kmz "$(DESTDIR)$(bindir)/takeout2kmz"

uninstall:
	rm -f "$(DESTDIR)$(bindir)/takeout2kmz"

clean:
	rm -f takeout2kmz
