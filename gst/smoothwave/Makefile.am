
plugin_LTLIBRARIES = libgstsmoothwave.la

libgstsmoothwave_la_SOURCES = gstsmoothwave.c
libgstsmoothwave_la_CFLAGS = $(GST_CFLAGS) 
libgstsmoothwave_la_LIBADD = 
libgstsmoothwave_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsmoothwave_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstsmoothwave.h

if HAVE_GTK
noinst_PROGRAMS = demo-osssrc
demo_osssrc_SOURCES = demo-osssrc.c
demo_osssrc_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
demo_osssrc_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)
endif

EXTRA_DIST = README
