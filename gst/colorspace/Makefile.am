plugin_LTLIBRARIES = libgstyuvtorgbcolorspace.la

if HAVE_CPU_I386
ARCHSRCS = yuv2rgb_mmx16.s
else
ARCHSRCS =
endif

libgstyuvtorgbcolorspace_la_SOURCES = gstcolorspace.c yuv2rgb.c
libgstyuvtorgbcolorspace_la_CFLAGS = $(GST_CFLAGS)
libgstyuvtorgbcolorspace_la_LIBADD = 
libgstyuvtorgbcolorspace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstyuvtorgbcolorspace_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = yuv2rgb.h gstcolorspace.h
