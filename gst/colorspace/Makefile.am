
plugin_LTLIBRARIES = libgstcolorspace.la

if HAVE_CPU_I386
ARCHSRCS = yuv2rgb_mmx16.s
else
ARCHSRCS =
endif

libgstcolorspace_la_SOURCES = gstcolorspace.c yuv2rgb.c
libgstcolorspace_la_CFLAGS = $(GST_CFLAGS)
libgstcolorspace_la_LIBADD = 
libgstcolorspace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = yuv2rgb.h gstcolorspace.h

