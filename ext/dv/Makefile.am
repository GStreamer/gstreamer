
plugin_LTLIBRARIES = libgstdvdec.la

libgstdvdec_la_SOURCES = gstdvdec.c
libgstdvdec_la_CFLAGS = $(GST_CFLAGS) $(LIBDV_CFLAGS)
libgstdvdec_la_LIBADD = $(LIBDV_LIBS) $(GST_BASE_LIBS)
libgstdvdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_PLUGINS_BASE_LIBS)

noinst_HEADERS = gstdvdec.h

#if HAVE_GTK
#noinst_PROGRAMS = demo-play
#endif

#demo_play_SOURCES = demo-play.c
#demo_play_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
#demo_play_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)

EXTRA_DIST = NOTES
