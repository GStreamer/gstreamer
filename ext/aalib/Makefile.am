
plugin_LTLIBRARIES = libgstaasink.la

libgstaasink_la_SOURCES =  gstaasink.c
libgstaasink_la_CFLAGS =  $(GST_CFLAGS) $(AALIB_CFLAGS) -I ../../../gst-plugins-base/gst-libs/
libgstaasink_la_LIBADD = $(AALIB_LIBS)
libgstaasink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS)

noinst_HEADERS = gstaasink.h
