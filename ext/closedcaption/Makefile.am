plugin_LTLIBRARIES = libgstclosedcaption.la

libgstclosedcaption_la_SOURCES = \
	gstccextractor.c \
	gstccextractor.h \
	gstclosedcaption.c

libgstclosedcaption_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstclosedcaption_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

libgstclosedcaption_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstccextractor.h
