# surf - simple browser
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC = surf.c
CSRC = common.c
WEBEXTSRC = libsurf-webext.c
OBJ = $(SRC:.c=.o)
COBJ = $(CSRC:.c=.o)
WEBEXTOBJ = $(WEBEXTSRC:.c=.o)

all: options libsurf-webext.so surf

options:
	@echo surf build options:
	@echo "CC            = $(CC)"
	@echo "CFLAGS        = $(SURFCFLAGS) $(CFLAGS)"
	@echo "WEBEXTCFLAGS  = $(WEBEXTCFLAGS) $(CFLAGS)"
	@echo "LDFLAGS       = $(LDFLAGS)"

.c.o:
	$(CC) $(SURFCFLAGS) $(CFLAGS) -c $<

config.h:
	cp config.def.h $@

$(OBJ): config.h common.h config.mk
$(COBJ): config.h common.h config.mk
$(WEBEXTOBJ): config.h common.h config.mk

$(WEBEXTOBJ): $(WEBEXTSRC)
	$(CC) $(WEBEXTCFLAGS) $(CFLAGS) -c $(WEBEXTSRC)

libsurf-webext.so: $(WEBEXTOBJ) $(COBJ)
	$(CC) -shared -Wl,-soname,$@ $(LDFLAGS) -o $@ \
	    $(WEBEXTOBJ) $(COBJ) $(WEBEXTLIBS)

surf: $(OBJ) $(COBJ)
	$(CC) $(SURFLDFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(COBJ) $(LIBS)

clean:
	rm -f surf $(OBJ) $(COBJ)
	rm -f libsurf-webext.so $(WEBEXTOBJ)

distclean: clean
	rm -f config.h surf-$(VERSION).tar.gz

dist: distclean
	mkdir -p surf-$(VERSION)
	cp -R LICENSE Makefile config.mk config.def.h README \
	    surf-open.sh arg.h TODO.md surf.png \
	    surf.1 $(SRC) $(WEBEXTSRC) surf-$(VERSION)
	tar -cf surf-$(VERSION).tar surf-$(VERSION)
	gzip surf-$(VERSION).tar
	rm -rf surf-$(VERSION)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f surf $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/surf
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp -f libsurf-webext.so $(DESTDIR)$(LIBDIR)
	chmod 644 $(DESTDIR)$(LIBDIR)/libsurf-webext.so
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < surf.1 > $(DESTDIR)$(MANPREFIX)/man1/surf.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/surf.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/surf
	rm -f $(DESTDIR)$(MANPREFIX)/man1/surf.1
	rm -f $(DESTDIR)$(LIBDIR)/libsurf-webext.so
	- rmdir $(DESTDIR)$(LIBDIR)

.SUFFIXES: .so .o .c
.PHONY: all options clean-dist clean dist install uninstall
