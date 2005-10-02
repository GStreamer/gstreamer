plugin_LTLIBRARIES = libgstflac.la

libgstflac_la_SOURCES = gstflac.c gstflacdec.c gstflacenc.c
#gstflacenc.c gstflactag.c
libgstflac_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir) $(GST_PLUGINS_BASE_CFLAGS)
libgstflac_la_LIBADD = $(GST_LIBS) $(FLAC_LIBS) $(GST_PLUGINS_BASE_LIBS) \
    -lgsttagedit-@GST_MAJORMINOR@
libgstflac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstflacenc.h gstflacdec.h flac_compat.h gstflactag.h
