plugin_LTLIBRARIES = libgstavtp.la

libgstavtp_la_SOURCES = \
	gstavtp.c \
	gstavtpaafpay.c \
	gstavtpbasepayload.c

libgstavtp_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(AVTP_CFLAGS)

libgstavtp_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(AVTP_LIBS)

libgstavtp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstavtpaafpay.h \
	gstavtpbasepayload.h
