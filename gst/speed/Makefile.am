
plugin_LTLIBRARIES = libgstspeed.la

libgstspeed_la_SOURCES = gstspeed.c
libgstspeed_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstspeed_la_LIBADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) $(LIBM)
libgstspeed_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstspeed_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstspeed.h

#if HAVE_GTK
#noinst_PROGRAMS = demo-mp3
#endif

#demo_mp3_SOURCES = demo-mp3.c
#demo_mp3_CFLAGS  = $(GTK_CFLAGS) $(GST_CFLAGS) 
#demo_mp3_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)
