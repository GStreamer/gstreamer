plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstaasink.la


libgstaasink_la_SOURCES =  gstaasink.c
libgstaasink_la_LIBADD = $(AALIB_LIBS)
libgstaasink_la_CFLAGS =  $(GST_CFLAGS) $(AALIB_CFLAGS)

noinst_HEADERS = gstaasink.h


