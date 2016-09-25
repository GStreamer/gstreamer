plugin_LTLIBRARIES = libgstbluez.la

libgstbluez_la_SOURCES = \
	bluez-plugin.c \
	gsta2dpsink.c \
	gstavdtpsink.c \
	gstavdtpsrc.c \
	gstavdtputil.c \
	gstavrcputil.c

nodist_libgstbluez_la_SOURCES = \
	$(BUILT_SOURCES)

libgstbluez_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
libgstbluez_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) \
	-lgstrtp-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS)
libgstbluez_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstbluez_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	a2dp-codecs.h \
	gsta2dpsink.h \
	gstavdtpsink.h \
	gstavdtpsrc.h \
	gstavdtputil.h \
	gstavrcputil.h

BUILT_SOURCES = \
	bluez.h \
	bluez.c

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = org.bluez.xml

bluez.h: bluez.c
bluez.c: org.bluez.xml
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
        --c-namespace=Bluez \
        --generate-c-code=bluez \
        --interface-prefix=org.bluez \
        $<
