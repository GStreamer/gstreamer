plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstsinesrc.la

libgstsinesrc_la_SOURCES = gstsinesrc.c
libgstsinesrc_la_CFLAGS = $(GST_CFLAGS)
libgstsinesrc_la_LIBADD = $(GST_LIBS)
libgstsinesrc_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gstsinesrc.h

if HAVE_GTK
noinst_PROGRAMS = demo-dparams
endif

demo_dparams_SOURCES = demo-dparams.c
## putting GTK in front solves a bad compilation problem
demo_dparams_CFLAGS  = $(GTK_CFLAGS) $(GST_CFLAGS) 
demo_dparams_LDFLAGS = $(GST_LIBS) $(GTK_LIBS) -lgstcontrol
