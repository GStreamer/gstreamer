filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstidct.la

libgstidct_la_SOURCES = fastintidct.c floatidct.c gstidct.c intidct.c mmxidct.S mmx32idct.c

bin_PROGRAMS = ieeetest

ieeetest_SOURCES = ieeetest.c
ieeetest_LDADD = libgstidct.la $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la
ieeetest_CFLAGS = $(shell gnome-config --cflags gnomeui) -g -Wall
ieeetest_LDFLAGS = $(shell gnome-config --libs gnomeui) 

noinst_HEADERS = gstidct.h

CFLAGS += -Wall -O2 -funroll-all-loops -finline-functions -ffast-math

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) -I$(top_srcdir)/include
LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la
