plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstplayondemand.la

libgstplayondemand_la_SOURCES = gstplayondemand.c
libgstplayondemand_la_CFLAGS = $(GST_CFLAGS)
libgstplayondemand_la_LIBADD = $(GST_LIBS)

noinst_HEADERS = gstplayondemand.h filter.func

if HAVE_GTK
noinst_PROGRAMS = demo-mp3
endif

demo_mp3_SOURCES = demo-mp3.c
demo_mp3_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
demo_mp3_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)

