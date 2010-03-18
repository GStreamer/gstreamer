plugin_LTLIBRARIES = libgstdirectsoundsink.la

libgstdirectsoundsink_la_SOURCES =  gstdirectsoundsink.c gstdirectsoundplugin.c
libgstdirectsoundsink_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(DIRECTSOUND_CFLAGS)
libgstdirectsoundsink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_MAJORMINOR) -lgstinterfaces-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(DIRECTSOUND_LIBS)
libgstdirectsoundsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DIRECTSOUND_LDFLAGS)
libgstdirectsoundsink_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstdirectsoundsink.h
