plugin_LTLIBRARIES = libgsttheora.la

libgsttheora_la_SOURCES = theora.c theoraenc.c theoradec.c
libgsttheora_la_CFLAGS = $(GST_CFLAGS) $(THEORA_CFLAGS)
libgsttheora_la_LIBADD = $(THEORA_LIBS) \
	$(top_builddir)/gst-libs/gst/tag/libgsttagedit-@GST_MAJORMINOR@.la
libgsttheora_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

