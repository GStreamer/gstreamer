plugin_LTLIBRARIES = libgstapexsink.la

libgstapexsink_la_SOURCES = gstapexplugin.c gstapexraop.c gstapexsink.c
libgstapexsink_la_CFLAGS = $(APEXSINK_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstapexsink_la_LIBADD = $(APEXSINK_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) -lgstaudio-$(GST_MAJORMINOR) -lgstinterfaces-$(GST_MAJORMINOR)
libgstapexsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstapexsink_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstapexraop.h gstapexsink.h
