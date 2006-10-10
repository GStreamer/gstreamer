
plugin_LTLIBRARIES = libgstdvbsrc.la

libgstdvbsrc_la_SOURCES = gstdvbsrc.c
libgstdvbsrc_la_CFLAGS = $(GST_CFLAGS) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
libgstdvbsrc_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS)
libgstdvbsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdvbsrc.h dvb-api/audio.h dvb-api/ca.h dvb-api/dmx.h \
		dvb-api/frontend.h dvb-api/net.h dvb-api/version.h \
		dvb-api/video.h 
