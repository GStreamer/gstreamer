filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstriff.la

libgstriff_la_SOURCES = gstriff.c

noinst_HEADERS = gstriff.h

CFLAGS += -O2 -Wall

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir)
LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la
