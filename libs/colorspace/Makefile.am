filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstcolorspace.la

if HAVE_CPU_I386
ARCHSRCS = yuv2rgb_mmx16.s
else
ARCHSRCS = 
endif

libgstcolorspace_la_SOURCES = gstcolorspace.c rgb2rgb.c yuv2rgb.c $(ARCHSRCS)

libgstcolorspaceincludedir = $(includedir)/gst/libs/gstcolorspace
libgstcolorspaceinclude_HEADERS = gstcolorspace.h

noinst_HEADERS = yuv2rgb.h

CFLAGS += -Wall -O2  -fomit-frame-pointer -finline-functions -ffast-math

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) -I$(top_srcdir)/include
LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la
