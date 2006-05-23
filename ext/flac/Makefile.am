plugin_LTLIBRARIES = libgstflac.la

# gstflactag.c
# gstflactag.h

libgstflac_la_SOURCES = gstflac.c gstflacdec.c gstflacenc.c
libgstflac_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir) $(GST_PLUGINS_BASE_CFLAGS)
libgstflac_la_LIBADD = $(GST_LIBS) $(FLAC_LIBS) $(GST_PLUGINS_BASE_LIBS) \
    -lgsttag-@GST_MAJORMINOR@
libgstflac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstflacenc.h gstflacdec.h
