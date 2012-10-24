
plugin_LTLIBRARIES = libgstremovesilence.la

libgstremovesilence_la_SOURCES = gstremovesilence.c vad_private.c
libgstremovesilence_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstremovesilence_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)
libgstremovesilence_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstremovesilence_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstremovesilence.h \
	vad_private.h
