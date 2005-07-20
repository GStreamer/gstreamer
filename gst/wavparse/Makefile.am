
plugin_LTLIBRARIES = libgstwavparse.la

libgstwavparse_la_SOURCES = gstwavparse.c
libgstwavparse_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
			-I$(top_srcdir)/gst-libs
libgstwavparse_la_LIBADD = -lgstriff-0.9
libgstwavparse_la_LDFLAGS = \
	         $(GST_PLUGIN_LDFLAGS) \
	         $(GST_PLUGINS_BASE_LIBS)

noinst_HEADERS = gstwavparse.h
