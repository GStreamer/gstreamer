plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstcolorspace.la

if HAVE_CPU_I386
ARCHSRCS = yuv2rgb_mmx16.s
else
ARCHSRCS =
endif

libcolorspace_la_SOURCES = gstcolorspace.c yuv2yuv.c yuv2rgb.c $(ARCHSRCS)
libcolorspace_la_CFLAGS = $(GST_CFLAGS)
if HAVE_LIBHERMES
libcolorspace_la_LIBADD = $(HERMES_LIBS)
endif

noinst_HEADERS = gstcolorspace.h yuv2rgb.h
