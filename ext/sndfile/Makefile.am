plugin_LTLIBRARIES = libgstsndfile.la

libgstsndfile_la_SOURCES = gstsf.c gstsfdec.c
libgstsndfile_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(SNDFILE_CFLAGS)
libgstsndfile_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(SNDFILE_LIBS)
libgstsndfile_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstsf.h gstsfdec.h
