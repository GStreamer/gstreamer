plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libaasink.la


libaasink_la_SOURCES =  aasink.c
libaasink_la_LIBADD = $(AALIB_LIBS)
libaasink_la_CFLAGS =  $(GST_CFLAGS) $(AALIB_CFLAGS)

noinst_HEADERS = aasink.h


