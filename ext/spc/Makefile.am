plugin_LTLIBRARIES = libgstspc.la

libgstspc_la_SOURCES = gstspc.c tag.c

libgstspc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(SPC_CFLAGS)
libgstspc_la_LIBADD = $(GST_LIBS) $(SPC_LIBS)
libgstspc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstspc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstspc.h tag.h

