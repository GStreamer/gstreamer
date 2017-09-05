plugin_LTLIBRARIES = libgstde265.la

libgstde265_la_SOURCES = \
	gstlibde265.c \
	libde265-dec.c

libgstde265_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	$(LIBDE265_CFLAGS)
libgstde265_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(GST_BASE_LIBS) $(GST_LIBS) \
	$(LIBDE265_LIBS)
libgstde265_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	libde265-dec.h
