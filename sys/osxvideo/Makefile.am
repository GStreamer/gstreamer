
plugin_LTLIBRARIES = libgstosxvideosrc.la

libgstosxvideosrc_la_SOURCES = osxvideoplugin.c osxvideosrc.c
libgstosxvideosrc_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) -Wno-deprecated-declarations
libgstosxvideosrc_la_LIBADD =  \
	$(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	-lgstinterfaces-$(GST_API_VERSION)

libgstosxvideosrc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

libgstosxvideosrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) \
	-Wl,-framework -Wl,Cocoa -Wl,-framework -Wl,QuickTime

noinst_HEADERS = osxvideosrc.h
