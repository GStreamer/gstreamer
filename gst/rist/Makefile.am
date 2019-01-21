plugin_LTLIBRARIES = libgstrist.la

libgstrist_la_SOURCES = \
	gstristsrc.c \
	gstristsink.c \
	gstristrtxsend.c \
	gstristrtxreceive.c \
	gstristplugin.c

noinst_HEADERS = \
	gstrist.h

libgstrist_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
                       $(GST_CFLAGS) \
                       $(GIO_CFLAGS)
libgstrist_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
		       $(GST_BASE_LIBS) \
		       -lgstrtp-@GST_API_VERSION@ \
		       $(GST_NET_LIBS) \
		       $(GST_LIBS) \
		       $(GIO_LIBS)
libgstrist_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrist_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
