plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstlibfame.la

libgstlibfame_la_SOURCES = gstlibfame.c	gstfamedec.c
libgstlibfame_la_CFLAGS = $(GST_CFLAGS)
libgstlibfame_la_LIBADD = $(LIBFAME_LIBS) 
libgstlibfame_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

