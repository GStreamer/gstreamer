noinst_PROGRAMS = test simple

test_SOURCES = test.c cothreads.c object.c looper.c
test_CFLAGS = $(shell gnome-config --cflags gnomeui)
test_LDFLAGS = $(shell gnome-config --libs gnomeui)

simple_SOURCES = simple.c cothreads.c
simple_CFLAGS = $(shell gnome-config --cflags gnomeui)
simple_LDFLAGS = $(shell gnome-config --libs gnomeui)

noinst_HEADERS = cothreads.h object.h looper.h

cothreads.c: $(top_srcdir)/gst/cothreads.c
	ln -sf $< $@
cothreads.h: $(top_srcdir)/gst/cothreads.h
	ln -sf $< $@
