plugin_LTLIBRARIES = libgstbluez.la

libgstbluez_la_SOURCES = \
	bluez-plugin.c \
	gsta2dpsink.c \
	gstavdtpsink.c \
	gstavdtpsrc.c \
	gstavdtputil.c

libgstbluez_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(DBUS_CFLAGS)
libgstbluez_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) \
	-lgstrtp-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(DBUS_LIBS)
libgstbluez_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstbluez_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	a2dp-codecs.h \
	gsta2dpsink.h \
	gstavdtpsink.h \
	gstavdtpsrc.h \
	gstavdtputil.h
