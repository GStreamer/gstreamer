filterdir = $(libdir)/gst

filter_LTLIBRARIES = libesdsink.la

libesdsink_la_SOURCES = esdsink.c
noinst_HEADERS = esdsink.h

libesdsink_la_CFLAGS = $(ESD_CFLAGS) $(GST_CFLAGS)
libesdsink_la_LIBADD = $(ESD_LIBS)

EXTRA_DIST = README
