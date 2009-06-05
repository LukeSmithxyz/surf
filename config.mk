# surf version
VERSION = 0.0

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

GTKINC=$(shell pkg-config --cflags gtk+-2.0 webkit-1.0)
GTKLIB=$(shell pkg-config --libs gtk+-2.0 webkit-1.0)


# includes and libs
INCS = -I. -I/usr/include ${GTKINC}
LIBS = -L/usr/lib -lc ${GTKLIB}

# flags
CPPFLAGS = -DVERSION=\"${VERSION}\" ${XINERAMAFLAGS}
#CFLAGS = -std=c99 -pedantic -Wall -Os ${INCS} ${CPPFLAGS}
CFLAGS = -std=c99 -pedantic -Wall -O0 ${INCS} ${CPPFLAGS}
#LDFLAGS = -s ${LIBS}
LDFLAGS = ${LIBS}

# Solaris
#CFLAGS = -fast ${INCS} -DVERSION=\"${VERSION}\"
#LDFLAGS = ${LIBS}

# compiler and linker
CC = cc
