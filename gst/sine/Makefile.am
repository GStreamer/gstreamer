plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstsinesrc.la

libgstsinesrc_la_SOURCES = gstsinesrc.c
libgstsinesrc_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = gstsinesrc.h

if HAVE_GTK
noinst_PROGRAMS = demo-dparams
endif

demo_dparams_SOURCES = demo-dparams.c
demo_dparams_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
demo_dparams_LDFLAGS = $(GST_LIBS) $(GTK_LIBS) -lgstcontrol
