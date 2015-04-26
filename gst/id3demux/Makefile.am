plugin_LTLIBRARIES = libgstid3demux.la

libgstid3demux_la_SOURCES = gstid3demux.c
libgstid3demux_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) 
libgstid3demux_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ $(GST_BASE_LIBS)
libgstid3demux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstid3demux_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstid3demux.h
