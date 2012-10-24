plugin_LTLIBRARIES = libgstwinks.la

libgstwinks_la_SOURCES = gstksclock.c \
	gstksvideodevice.c \
	gstksvideosrc.c \
	kshelpers.c kshelpers.h \
	ksvideohelpers.c 
libgstwinks_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(DIRECTX_CFLAGS)
libgstwinks_la_LIBADD = \
	$(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	-lgstinterfaces-$(GST_MAJORMINOR) $(DIRECTX_LDFLAGS) $(DIRECTSHOW_LIBS) 
libgstwinks_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwinks_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS= gstksclock.h

