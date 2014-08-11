plugin_LTLIBRARIES = libgstwebp.la

noinst_HEADERS = gstwebpdec.h gstwebpenc.h

libgstwebp_la_SOURCES = gstwebp.c \
			gstwebpdec.c \
			gstwebpenc.c

libgstwebp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(WEBP_CFLAGS)
libgstwebp_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(WEBP_LIBS)
libgstwebp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwebp_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

