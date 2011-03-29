plugin_LTLIBRARIES = libgstpulse.la

libgstpulse_la_SOURCES = \
	plugin.c \
	pulsemixer.c \
	pulsemixerctrl.c \
	pulsemixertrack.c \
	pulseprobe.c \
	pulsesink.c \
	pulseaudiosink.c \
	pulsesrc.c \
	pulseutil.c

libgstpulse_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(PULSE_CFLAGS)
libgstpulse_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) \
	-lgstinterfaces-$(GST_MAJORMINOR) -lgstpbutils-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(PULSE_LIBS)
libgstpulse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpulse_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	pulsemixerctrl.h \
	pulsemixer.h \
	pulsemixertrack.h \
	pulseprobe.h \
	pulsesink.h \
	pulsesrc.h \
	pulseutil.h

