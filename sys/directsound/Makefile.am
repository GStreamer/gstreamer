plugin_LTLIBRARIES = libgstdirectsoundsink.la

libgstdirectsoundsink_la_SOURCES =  gstdirectsoundsink.c gstdirectsoundplugin.c
libgstdirectsoundsink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(DIRECTSOUND_CFLAGS)
libgstdirectsoundsink_la_LIBADD = $(DIRECTSOUND_LIBS) \
	$(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_MAJORMINOR) -lgstinterfaces-$(GST_MAJORMINOR)
libgstdirectsoundsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DIRECTSOUND_LDFLAGS)

noinst_HEADERS = gstdirectsoundsink.h
