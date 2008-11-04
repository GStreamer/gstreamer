plugin_LTLIBRARIES = libgstxvid.la

libgstxvid_la_SOURCES = gstxvidenc.c gstxviddec.c gstxvid.c
libgstxvid_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(XVID_CFLAGS)
libgstxvid_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(XVID_LIBS)
libgstxvid_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstxvid_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstxvidenc.h gstxviddec.h gstxvid.h
