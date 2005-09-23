plugin_LTLIBRARIES = libgstspeex.la

libgstspeex_la_SOURCES = gstspeex.c gstspeexdec.c
#gstspeexenc.c
libgstspeex_la_CFLAGS = $(GST_CFLAGS) $(SPEEX_CFLAGS)
libgstspeex_la_LIBADD = $(GST_LIBS) $(SPEEX_LIBS)
libgstspeex_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstspeexenc.h gstspeexdec.h 
