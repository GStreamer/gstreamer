plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstdvdec.la

libgstdvdec_la_SOURCES = gstdvdec.c
libgstdvdec_la_CFLAGS = $(GST_CFLAGS)
libgstdvdec_la_LIBADD = $(DV_LIBS) 
libgstdvdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdvdec.h

#if HAVE_GTK
#noinst_PROGRAMS = demo-play
#endif

#demo_play_SOURCES = demo-play.c
#demo_play_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
#demo_play_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)

EXTRA_DIST = NOTES
