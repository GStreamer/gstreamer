plugin_LTLIBRARIES = libgstmonoscope.la

libgstmonoscope_la_SOURCES = gstmonoscope.c monoscope.c convolve.c

noinst_HEADERS = gstmonoscope.h monoscope.h convolve.h

libgstmonoscope_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
libgstmonoscope_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstmonoscope_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmonoscope_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
