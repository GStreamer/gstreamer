
plugin_LTLIBRARIES = libgstxvid.la

libgstxvid_la_SOURCES = gstxvidenc.c gstxviddec.c gstxvid.c
libgstxvid_la_CFLAGS = $(GST_CFLAGS) $(XVID_CFLAGS)
libgstxvid_la_LIBADD = $(XVID_LIBS)
libgstxvid_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstxvidenc.h gstxviddec.h
