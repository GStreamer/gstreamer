plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmad.la

libgstmad_la_SOURCES = gstmad.c	
libgstmad_la_CFLAGS = $(GST_CFLAGS)
libgstmad_la_LIBADD = $(MAD_LIBS) 
libgstmad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

