filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstcolorspace.la

libgstcolorspace_la_SOURCES = gstcolorspace.c rgb2rgb.c yuv2rgb.c

libgstcolorspaceincludedir = $(includedir)/gst/libs/gstcolorspace.h
libgstcolorspaceinclude_HEADERS = gstcolorspace.h

noinst_HEADERS = yuv2rgb.h

CFLAGS += -Wall -O2  -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) -I$(top_srcdir)/include
LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la
