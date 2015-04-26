plugin_LTLIBRARIES = libgstalpha.la libgstalphacolor.la

libgstalpha_la_SOURCES = gstalpha.c
libgstalpha_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstalpha_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstalpha_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstalpha_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

libgstalphacolor_la_SOURCES = gstalphacolor.c
libgstalphacolor_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstalphacolor_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstalphacolor_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstalphacolor_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstalpha.h gstalphacolor.h
