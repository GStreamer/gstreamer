filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstgetbits.la

libgstgetbits_la_SOURCES = gstgetbits.c gstgetbits_inl.h 

libgstgetbitsincludedir = $(includedir)/gst/libs/gstgetbits.h
libgstgetbitsinclude_HEADERS = gstgetbits.h

noinst_HEADERS = gstgetbits.h gstgetbits_inl.h

CFLAGS += -Wall -O2  -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) -I$(top_srcdir)/include
LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la
