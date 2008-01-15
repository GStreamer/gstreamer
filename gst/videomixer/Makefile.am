plugin_LTLIBRARIES = libgstvideomixer.la

libgstvideomixer_la_SOURCES = videomixer.c
libgstvideomixer_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstvideomixer_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstvideomixer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = videomixer.h videomixerpad.h
