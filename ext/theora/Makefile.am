plugin_LTLIBRARIES = libgsttheora.la

noinst_HEADERS = gsttheoraenc.h \
		 gsttheoradec.h \
		 gsttheoraparse.h

libgsttheora_la_SOURCES = gsttheora.c \
			  gsttheoraenc.c \
			  gsttheoradec.c \
			  gsttheoraparse.c

libgsttheora_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(THEORA_CFLAGS)
libgsttheora_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(THEORA_LIBS)
libgsttheora_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttheora_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

