plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmedian.la

libgstmedian_la_SOURCES = gstmedian.c

libgstmedian_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math $(GST_CFLAGS)
libgstmedian_la_LIBADD = $(GST_LIBS)

noinst_HEADERS = gstmedian.h
