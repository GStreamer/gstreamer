plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstindexers.la

# file index uses xml
if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC =
else
GST_LOADSAVE_SRC = gstfileindex.c
endif

libgstindexers_la_SOURCES = gstindexers.c gstmemindex.c $(GST_LOADSAVE_SRC)
libgstindexers_la_CFLAGS = $(GST_CFLAGS)
libgstindexers_la_LIBADD =
libgstindexers_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
