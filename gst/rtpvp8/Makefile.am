plugin_LTLIBRARIES = libgstrtpvp8.la

libgstrtpvp8_la_SOURCES = gstrtpvp8.c \
  gstrtpvp8depay.c \
  gstrtpvp8pay.c

noinst_HEADERS = gstrtpvp8depay.h \
  gstrtpvp8pay.h

libgstrtpvp8_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtpvp8_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_MAJORMINOR@ \
	$(GST_LIBS)
libgstrtpvp8_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtpvp8_la_LIBTOOLFLAGS = --tag=disable-static
