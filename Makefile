# surf - simple browser
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC = surf.c common.c
OBJ = $(SRC:.c=.o)
LIBSRC = libsurf-webext.c common.c
LIBOBJ = $(LIBSRC:.c=.lo)

all: options libsurf-webext.la surf

options:
	@echo surf build options:
	@echo "CFLAGS     = $(SURFCFLAGS)"
	@echo "LDFLAGS    = $(SURFLDFLAGS)"
	@echo "CC         = $(CC)"
	@echo "LIBCFLAGS  = $(LIBCFLAGS)"
	@echo "LIBLDFLAGS = $(LIBLDFLAGS)"
	@echo "LIBTOOL    = $(LIBTOOL)"

.c.o:
	$(CC) $(SURFCFLAGS) -c $<

.c.lo:
	$(LIBTOOL) --mode compile --tag CC $(CC) $(LIBCFLAGS) -c $<

$(OBJ): config.h config.mk
$(LIBOBJ): config.h config.mk

config.h:
	cp config.def.h $@

libsurf-webext.la: $(LIBOBJ)
	$(LIBTOOL) --mode link --tag CC $(CC) $(LIBLDFLAGS) -o $@ \
	    $(LIBOBJ) $(LIB) -rpath $(DESTDIR)$(LIBPREFIX)

surf: $(OBJ)
	$(CC) $(SURFCFLAGS) -o $@ $(OBJ) $(SURFLDFLAGS)

clean-lib:
	rm -rf libsurf-webext.la .libs $(LIBOBJ) $(LIBOBJ:.lo=.o)

clean: clean-lib
	rm -f surf $(OBJ)

distclean: clean
	rm -f config.h surf-$(VERSION).tar.gz

dist: distclean
	mkdir -p surf-$(VERSION)
	cp -R LICENSE Makefile config.mk config.def.h README \
	    surf-open.sh arg.h TODO.md surf.png \
	    surf.1 $(SRC) $(LIBSRC) surf-$(VERSION)
	tar -cf surf-$(VERSION).tar surf-$(VERSION)
	gzip surf-$(VERSION).tar
	rm -rf surf-$(VERSION)

install-lib: libsurf-webext.la
	mkdir -p $(DESTDIR)$(LIBPREFIX)
	$(LIBTOOL) --mode install install -c libsurf-webext.la \
	    $(DESTDIR)$(LIBPREFIX)/libsurf-webext.la

install: all install-lib
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f surf $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/surf
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < surf.1 > $(DESTDIR)$(MANPREFIX)/man1/surf.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/surf.1

uninstall-lib:
	$(LIBTOOL) --mode uninstall rm -f \
	    $(DESTDIR)$(LIBPREFIX)/libsurf-webext.la
	- rm -df $(DESTDIR)$(LIBPREFIX)

uninstall: uninstall-lib
	rm -f $(DESTDIR)$(PREFIX)/bin/surf
	rm -f $(DESTDIR)$(MANPREFIX)/man1/surf.1

.SUFFIXES: .la .lo .o .c
.PHONY: all options clean-dist clean dist install-lib install uninstall-lib uninstall
