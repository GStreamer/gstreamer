noinst_LTLIBRARIES = libcrashtest.la

libcrashtest_la_SOURCES = \
	dummy.c

include_HEADERS = \
	dummy.h

CFLAGS = $(shell gnome-config --cflags gnomeui)
LDFLAGS = $(shell gnome-config --libs gnomeui)

noinst_PROGRAMS = test
test_LDADD = libcrashtest.la
