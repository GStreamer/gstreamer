plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstspeed.la

libgstspeed_la_SOURCES = gstspeed.c
libgstspeed_la_CFLAGS = $(GST_CFLAGS)
libgstspeed_la_LIBADD = $(GST_LIBS)

noinst_HEADERS = gstspeed.h filter.func

if HAVE_GTK
noinst_PROGRAMS = demo-mp3
endif

demo_mp3_SOURCES = demo-mp3.c
demo_mp3_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
demo_mp3_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)
