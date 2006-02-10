plugin_LTLIBRARIES = libgstcdxaparse.la

#	gstcdxastrip.c
#	gstcdxastrip.h

libgstcdxaparse_la_SOURCES = \
	gstcdxaparse.c


noinst_HEADERS = \
	gstcdxaparse.h

libgstcdxaparse_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)

libgstcdxaparse_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstriff-@GST_MAJORMINOR@

libgstcdxaparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
