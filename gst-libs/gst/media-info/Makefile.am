librarydir = $(libdir)

library_LTLIBRARIES = libgstmedia-info.la

libgstmedia_info_la_SOURCES = media-info.c

libincludedir = $(includedir)/@PACKAGE@-@VERSION@/gst/media-info
libinclude_HEADERS = media-info.h

libgstmedia_info_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGIN_CFLAGS)
libgstmedia_info_la_LIBADD = $(GST_LIBS) $(GST_PLUGIN_LIBS)

noinst_PROGRAMS = media-info-test

media_info_test_SOURCES = media-info-test.c
media_info_test_CFLAGS = $(GST_CFLAGS)
media_info_test_LDADD = $(GST_LIBS) libgstmedia-info.la
