
plugin_LTLIBRARIES = libgstmonoscope.la

libgstmonoscope_la_SOURCES = gstmonoscope.c monoscope.c convolve.c

noinst_HEADERS = monoscope.h convolve.h

libgstmonoscope_la_CFLAGS = -O2 $(GST_FFASTMATH_CFLAGS) $(GST_CFLAGS)
libgstmonoscope_la_LIBADD =
libgstmonoscope_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

