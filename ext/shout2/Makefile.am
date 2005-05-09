
plugin_LTLIBRARIES = libgstshout2.la

libgstshout2_la_SOURCES = gstshout2.c 
libgstshout2_la_CFLAGS = $(SHOUT2_CFLAGS) $(GST_CFLAGS)
libgstshout2_la_LIBADD = $(SHOUT2_LIBS) $(GST_LIBS) $(GST_BASE_LIBS)
libgstshout2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstshout2.h
