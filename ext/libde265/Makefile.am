plugin_LTLIBRARIES = libgstlibde265.la

libgstlibde265_la_SOURCES = \
	gstlibde265.c \
	libde265-dec.c

libgstlibde265_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	$(LIBDE265_CFLAGS)
libgstlibde265_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(GST_BASE_LIBS) $(GST_LIBS) \
	$(LIBDE265_LIBS)
libgstlibde265_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstlibde265_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	libde265-dec.h
