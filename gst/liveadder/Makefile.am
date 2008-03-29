plugin_LTLIBRARIES = libgstliveadder.la

libgstliveadder_la_SOURCES = liveadder.c
libgstliveadder_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(ERROR_CFLAGS)
libgstliveadder_la_LIBADD = $(GST_LIBS_LIBS)
libgstliveadder_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstaudio-0.10

noinst_HEADERS = liveadder.h
