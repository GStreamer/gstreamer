filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstsynaesthesia.la

libgstsynaesthesia_la_SOURCES = core.c gstsynaesthesia.c
libgstsynaesthesia_la_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS)
libgstsynaesthesia_la_LIBADD = $(GTK_LIBS)

noinst_HEADERS = core.h gstsynaesthesia.h

EXTRA_DIST = README README-syna
