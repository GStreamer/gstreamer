
plugin_LTLIBRARIES = libgstsndfile.la

libgstsndfile_la_SOURCES = gstsf.c
libgstsndfile_la_CFLAGS = $(GST_CFLAGS) $(SNDFILE_CFLAGS)
libgstsndfile_la_LIBADD = $(SNDFILE_LIBS)
libgstsndfile_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstsf.h
