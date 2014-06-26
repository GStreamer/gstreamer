plugin_LTLIBRARIES = libgstpulse.la

libgstpulse_la_SOURCES = \
	plugin.c \
	pulsesink.c \
	pulsesrc.c \
	pulsedeviceprovider.c \
	pulseutil.c

libgstpulse_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(PULSE_CFLAGS)
libgstpulse_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(PULSE_LIBS)
libgstpulse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpulse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	pulsesink.h \
	pulsesrc.h \
	pulsedeviceprovider.h \
	pulseutil.h

