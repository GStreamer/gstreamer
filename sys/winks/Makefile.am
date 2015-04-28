plugin_LTLIBRARIES = libgstwinks.la

libgstwinks_la_SOURCES = gstksclock.c \
	gstksvideodevice.c \
	gstksvideosrc.c \
	ksdeviceprovider.c ksdeviceprovider.h \
	kshelpers.c kshelpers.h \
	ksvideohelpers.c 
libgstwinks_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(DIRECTX_CFLAGS) -Wno-error=missing-braces
libgstwinks_la_LIBADD = \
	$(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(DIRECTX_LDFLAGS) $(WINKS_LIBS)
libgstwinks_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwinks_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS= gstksclock.h ksvideohelpers.h gstksvideodevice.h gstksvideosrc.h

