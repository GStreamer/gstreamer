plugin_LTLIBRARIES = libgstspeex.la

libgstspeex_la_SOURCES = gstspeex.c gstspeexdec.c gstspeexenc.c
libgstspeex_la_CFLAGS = $(GST_CFLAGS) $(SPEEX_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstspeex_la_LIBADD = $(GST_LIBS) $(SPEEX_LIBS) $(GST_PLUGINS_BASE_LIBS) \
    -lgsttagedit-@GST_MAJORMINOR@
libgstspeex_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS)

noinst_HEADERS = gstspeexenc.h gstspeexdec.h 
