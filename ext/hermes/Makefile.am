plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstcolorspace.la

if HAVE_CPU_I386
ARCHSRCS = yuv2rgb_mmx16.s
else
ARCHSRCS =
endif

if USE_HERMES
PLUGIN_EXTRA_LIBS = $(HERMES_LIBS)
else
PLUGIN_EXTRA_LIBS =
endif

libgstcolorspace_la_SOURCES = gstcolorspace.c yuv2yuv.c yuv2rgb.c $(ARCHSRCS)
libgstcolorspace_la_CFLAGS = $(GST_CFLAGS)
libgstcolorspace_la_LIBADD = $(GST_LIBS) $(PLUGIN_EXTRA_LIBS)

noinst_HEADERS = gstcolorspace.h yuv2rgb.h
