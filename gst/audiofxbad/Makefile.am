plugin_LTLIBRARIES = libgstaudiofxbad.la

libgstaudiofxbad_la_SOURCES = gstaudiofxbad.c \
    gstaudiochannelmix.c gstaudiochannelmix.h

libgstaudiofxbad_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstaudiofxbad_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS)  $(GST_LIBS) $(LIBM)
libgstaudiofxbad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
