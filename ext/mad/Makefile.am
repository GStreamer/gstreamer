plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmad.la

libgstmad_la_SOURCES = gstmad.c	
libgstmad_la_CFLAGS = $(GST_CFLAGS)
libgstmad_la_LIBADD = $(GST_LIBS) $(MAD_LIBS) 

noinst_HEADERS = gstmad.h
