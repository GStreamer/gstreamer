plugin_LTLIBRARIES = libgsttheora.la

libgsttheora_la_SOURCES = theora.c theoraenc.c theoradec.c
libgsttheora_la_CFLAGS = $(GST_CFLAGS) $(THEORA_CFLAGS)
libgsttheora_la_LIBADD = $(THEORA_LIBS) $(top_builddir)/gst/tags/libgsttagedit.la
libgsttheora_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

