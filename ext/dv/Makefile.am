
plugin_LTLIBRARIES = libgstdv.la

libgstdv_la_SOURCES = gstdv.c gstdvdec.c gstdvdemux.c
libgstdv_la_CFLAGS = $(GST_CFLAGS) $(LIBDV_CFLAGS)
libgstdv_la_LIBADD = $(LIBDV_LIBS) $(GST_BASE_LIBS)
libgstdv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_PLUGINS_BASE_LIBS)

noinst_HEADERS = gstdvdemux.h gstdvdec.h

#if HAVE_GTK
#noinst_PROGRAMS = demo-play
#endif

#demo_play_SOURCES = demo-play.c
#demo_play_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
#demo_play_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)

EXTRA_DIST = NOTES
