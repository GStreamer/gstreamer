plugin_LTLIBRARIES = libgstcdxaparse.la

libgstcdxaparse_la_SOURCES = \
	gstcdxaparse.c \
	gstvcdparse.c


noinst_HEADERS = \
	gstcdxaparse.h \
	gstvcdparse.h

libgstcdxaparse_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)

libgstcdxaparse_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstriff-@GST_API_VERSION@

libgstcdxaparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcdxaparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
