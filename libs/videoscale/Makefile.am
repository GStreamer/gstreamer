filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstvideoscale.la

libgstvideoscale_la_SOURCES = gstvideoscale.c 

libgstvideoscaleincludedir = $(includedir)/gst/libs/gstvideoscale
libgstvideoscaleinclude_HEADERS = gstvideoscale.h

noinst_HEADERS = yuv2rgb.h

CFLAGS += -Wall -O1 -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) -I$(top_srcdir)/include
LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la
