plugin_LTLIBRARIES = libgstflv.la

libgstflv_la_CFLAGS = ${GST_CFLAGS}
libgstflv_la_LIBADD = $(GST_BASE_LIBS)
libgstflv_la_LDFLAGS = ${GST_PLUGIN_LDFLAGS} 
libgstflv_la_SOURCES = gstflvdemux.c gstflvparse.c gstflvmux.c

noinst_HEADERS = gstflvdemux.h gstflvparse.h gstflvmux.h
