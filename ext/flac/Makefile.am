plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstflac.la

libgstflac_la_SOURCES = gstflac.c gstflacenc.c gstflacdec.c

noinst_HEADERS = gstflacenc.h gstflacdec.h

libgstflac_la_CFLAGS = $(GST_CFLAGS)
libgstflac_la_LIBADD = $(GST_LIBS) $(FLAC_LIBS) 
