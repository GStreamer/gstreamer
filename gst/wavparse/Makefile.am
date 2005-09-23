plugin_LTLIBRARIES = libgstwavparse.la

libgstwavparse_la_SOURCES = gstwavparse.c
libgstwavparse_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
			-I$(top_srcdir)/gst-libs
libgstwavparse_la_LIBADD = -lgstriff-@GST_MAJORMINOR@ $(GST_PLUGINS_BASE_LIBS)
libgstwavparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstwavparse.h
