plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmp1videoparse.la

libgstmp1videoparse_la_SOURCES = gstmp1videoparse.c
libgstmp1videoparse_la_CFLAGS = $(GST_CFLAGS) -O3 $(FOMIT_FRAME_POINTER) -ffast-math 
libgstmp1videoparse_la_LIBADD = $(GST_LIBS)

noinst_HEADERS = gstmp1videoparse.h
