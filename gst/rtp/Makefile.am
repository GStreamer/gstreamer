plugin_LTLIBRARIES = libgstrtpbad.la

libgstrtpbad_la_SOURCES = \
	gstrtp.c \
	gstrtph265depay.c \
	gstrtph265pay.c

libgstrtpbad_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstrtpbad_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-$(GST_API_VERSION).la \
	-lgstaudio-@GST_API_VERSION@ \
	-lgstvideo-@GST_API_VERSION@ \
	-lgsttag-@GST_API_VERSION@ \
	-lgstrtp-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) \
	$(LIBM)
libgstrtpbad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtpbad_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS =			\
	gstrtph265depay.h \
	gstrtph265pay.h
