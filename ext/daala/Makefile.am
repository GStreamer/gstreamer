plugin_LTLIBRARIES = libgstdaala.la

noinst_HEADERS = gstdaalaenc.h \
		 gstdaaladec.h

libgstdaala_la_SOURCES = gstdaala.c \
			  gstdaalaenc.c \
			  gstdaaladec.c

libgstdaala_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(DAALA_CFLAGS)
libgstdaala_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgsttag-$(GST_API_VERSION) \
	-lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(DAALA_LIBS)
libgstdaala_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

