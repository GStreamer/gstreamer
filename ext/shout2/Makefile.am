plugin_LTLIBRARIES = libgstshout2.la

libgstshout2_la_SOURCES = gstshout2.c 
libgstshout2_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(SHOUT2_CFLAGS)
libgstshout2_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(SHOUT2_LIBS)
libgstshout2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstshout2_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstshout2.h
