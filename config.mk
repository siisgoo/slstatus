# slstatus version
VERSION = 0

# customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

NOTIFINC=\
	-I/usr/include/libmount\
	-I/usr/include/blkid\
	-pthread

INC=$(GLIBINC) $(NOTIFINC)

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

# flags
CPPFLAGS = -I$(X11INC) $(INC) -D_DEFAULT_SOURCE
CFLAGS   = -std=c99 -pedantic -Wall -Wextra -O3
LDFLAGS  = -L$(X11LIB) -s
LDLIBS=\
	-lX11\

# compiler and linker
CC = gcc
