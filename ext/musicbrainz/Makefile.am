plugin_LTLIBRARIES = libgsttrm.la

noinst_HEADERS = gsttrm.h 

libgsttrm_la_SOURCES = gsttrm.c
libgsttrm_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(MUSICBRAINZ_CFLAGS)
libgsttrm_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) $(MUSICBRAINZ_LIBS) \
	-lgsttag-@GST_MAJORMINOR@
libgsttrm_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttrm_la_LIBTOOLFLAGS = --tag=disable-static

