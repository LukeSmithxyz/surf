# surf - simple browser
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC = surf.c
OBJ = $(SRC:.c=.o)
LIBSRC = libsurf-webext.c
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
	@echo CC -c $<
	@$(CC) $(SURFCFLAGS) -c $<

.c.lo:
	@echo libtool compile $<
	@$(LIBTOOL) --mode compile --tag CC $(CC) $(LIBCFLAGS) -c $<

$(OBJ): config.h config.mk
$(LIBOBJ): config.mk

config.h:
	@echo creating $@ from config.def.h
	@cp config.def.h $@

libsurf-webext.la: $(LIBOBJ)
	@echo libtool link $@
	@$(LIBTOOL) --mode link --tag CC $(CC) $(LIBLDFLAGS) -o $@ \
	    $(LIBOBJ) -rpath $(DESTDIR)$(LIBPREFIX)

surf: $(OBJ)
	@echo CC -o $@
	@$(CC) $(SURFCFLAGS) -o $@ $(OBJ) $(SURFLDFLAGS)

clean-lib:
	@echo cleaning library
	@rm -rf libsurf-webext.la .libs $(LIBOBJ) $(LIBOBJ:.lo=.o)

clean: clean-lib
	@echo cleaning
	@rm -f surf $(OBJ)

distclean: clean
	@echo cleaning dist
	@rm -f config.h surf-$(VERSION).tar.gz

dist: distclean
	@echo creating dist tarball
	@mkdir -p surf-$(VERSION)
	@cp -R LICENSE Makefile config.mk config.def.h README \
	    surf-open.sh arg.h TODO.md surf.png \
	    surf.1 $(SRC) $(LIBSRC) surf-$(VERSION)
	@tar -cf surf-$(VERSION).tar surf-$(VERSION)
	@gzip surf-$(VERSION).tar
	@rm -rf surf-$(VERSION)

install-lib: libsurf-webext.la
	@echo installing library file to $(DESTDIR)$(LIBPREFIX)
	@mkdir -p $(DESTDIR)$(LIBPREFIX)
	@$(LIBTOOL) --mode install install -c libsurf-webext.la \
	    $(DESTDIR)$(LIBPREFIX)/libsurf-webext.la

install: all install-lib
	@echo installing executable file to $(DESTDIR)$(PREFIX)/bin
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@cp -f surf $(DESTDIR)$(PREFIX)/bin
	@chmod 755 $(DESTDIR)$(PREFIX)/bin/surf
	@echo installing manual page to $(DESTDIR)$(MANPREFIX)/man1
	@mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	@sed "s/VERSION/$(VERSION)/g" < surf.1 > $(DESTDIR)$(MANPREFIX)/man1/surf.1
	@chmod 644 $(DESTDIR)$(MANPREFIX)/man1/surf.1

uninstall-lib:
	@echo removing library file from $(DESTDIR)$(LIBPREFIX)
	@$(LIBTOOL) --mode uninstall rm -f \
	    $(DESTDIR)$(LIBPREFIX)/libsurf-webext.la
	@- rm -df $(DESTDIR)$(LIBPREFIX)

uninstall: uninstall-lib
	@echo removing executable file from $(DESTDIR)$(PREFIX)/bin
	@rm -f $(DESTDIR)$(PREFIX)/bin/surf
	@echo removing manual page from $(DESTDIR)$(MANPREFIX)/man1
	@rm -f $(DESTDIR)$(MANPREFIX)/man1/surf.1

.SUFFIXES: .la .lo .o .c
.PHONY: all options clean-dist clean dist install-lib install uninstall-lib uninstall
