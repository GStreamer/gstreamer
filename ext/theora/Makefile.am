plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgsttheora.la

libgsttheora_la_SOURCES = theora.c theoradec.c theoraenc.c
libgsttheora_la_CFLAGS = $(GST_CFLAGS) $(THEORA_CFLAGS) 
libgsttheora_la_LIBADD = $(THEORA_LIBS) #-ltheora -lvorbis
libgsttheora_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

