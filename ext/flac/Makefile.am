
plugin_LTLIBRARIES = libgstflac.la

libgstflac_la_SOURCES = gstflac.c gstflacenc.c gstflacdec.c gstflactag.c
libgstflac_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)
libgstflac_la_LIBADD = $(FLAC_LIBS) 
libgstflac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstflacenc.h gstflacdec.h flac_compat.h gstflactag.h
