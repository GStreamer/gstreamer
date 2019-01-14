plugin_LTLIBRARIES = libgstavtp.la

libgstavtp_la_SOURCES = gstavtp.c

libgstavtp_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(AVTP_CFLAGS)

libgstavtp_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(AVTP_LIBS)

libgstavtp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
