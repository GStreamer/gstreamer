filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstriff.la

libgstriff_la_SOURCES = gstriffparse.c gstriffencode.c gstriffutil.c

noinst_HEADERS = gstriff.h

CFLAGS += -Wall -O2  -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir)
LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la
