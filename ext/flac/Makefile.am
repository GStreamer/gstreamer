plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstflac.la

libgstflac_la_SOURCES = gstflac.c gstflacenc.c gstflacdec.c
libgstflac_la_CFLAGS = $(GST_CFLAGS)
libgstflac_la_LIBADD = $(GST_LIBS) $(FLAC_LIBS) 
libgstflac_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gstflacenc.h gstflacdec.h
