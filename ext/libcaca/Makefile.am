
plugin_LTLIBRARIES = libgstcacasink.la

libgstcacasink_la_SOURCES =  gstcacasink.c
libgstcacasink_la_CFLAGS =  $(GST_CFLAGS) $(CACALIB_CFLAGS)
libgstcacasink_la_LIBADD = $(CACALIB_LIBS)
libgstcacasink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstcacasink.h
