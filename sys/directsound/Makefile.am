plugin_LTLIBRARIES = libgstdirectsoundsrc.la

libgstdirectsoundsrc_la_SOURCES =  gstdirectsoundsrc.c gstdirectsoundplugin.c
libgstdirectsoundsrc_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(DIRECTX_CFLAGS)
libgstdirectsoundsrc_la_LIBADD = $(DIRECTDRAW_LIBS) \
	$(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) \
	-lgstinterfaces-$(GST_MAJORMINOR) -ldsound
libgstdirectsoundsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DIRECTX_LDFLAGS)
libgstdirectsoundsrc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS= gstdirectsoundsrc.h
