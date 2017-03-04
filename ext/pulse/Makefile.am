plugin_LTLIBRARIES = libgstpulseaudio.la

libgstpulseaudio_la_SOURCES = \
	plugin.c \
	pulsesink.c \
	pulsesrc.c \
	pulsedeviceprovider.c \
	pulseutil.c

libgstpulseaudio_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(PULSE_CFLAGS)
libgstpulseaudio_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(PULSE_LIBS)
libgstpulseaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpulseaudio_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	pulsesink.h \
	pulsesrc.h \
	pulsedeviceprovider.h \
	pulseutil.h

