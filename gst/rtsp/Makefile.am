plugin_LTLIBRARIES = libgstrtsp.la

libgstrtsp_la_SOURCES = gstrtsp.c gstrtspsrc.c \
			gstrtpdec.c gstrtspext.c

libgstrtsp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtsp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
		       -lgstinterfaces-@GST_MAJORMINOR@ \
		       -lgstrtp-@GST_MAJORMINOR@ -lgstrtsp-@GST_MAJORMINOR@ \
		       -lgstsdp-@GST_MAJORMINOR@ $(GST_LIBS) $(WIN32_LIBS)
libgstrtsp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtsp_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstrtspsrc.h     \
		 gstrtsp.h        \
		 gstrtpdec.h      \
		 gstrtspext.h
