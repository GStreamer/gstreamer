filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstputbits.la

libgstputbits_la_SOURCES = gstputbits.c 

libgstputbitsincludedir = $(includedir)/gst/libs/gstputbits
libgstputbitsinclude_HEADERS = gstputbits.h

noinst_HEADERS = gstputbits.h 

CFLAGS += -Wall -O2  -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) -I$(top_srcdir)/include
LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la
