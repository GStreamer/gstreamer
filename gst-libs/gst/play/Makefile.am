librarydir = $(libdir)

library_LTLIBRARIES = libgstplay.la

libgstplay_la_SOURCES = play.c

libgstplayincludedir = $(includedir)/@PACKAGE@-@VERSION@/gst/play
libgstplayinclude_HEADERS = play.h

libgstplay_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_CFLAGS)
libgstplay_la_LIBADD = $(GST_LIBS) $(GST_PLUGINS_LIBS)

noinst_HEADERS = playpipelines.c
