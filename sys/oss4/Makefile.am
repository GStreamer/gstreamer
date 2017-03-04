plugin_LTLIBRARIES = libgstoss4.la

libgstoss4_la_SOURCES = \
	oss4-audio.c          \
	oss4-property-probe.c \
	oss4-sink.c           \
	oss4-source.c

libgstoss4_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstoss4_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) \
	$(GST_LIBS)
libgstoss4_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 
libgstoss4_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	oss4-audio.h          \
	oss4-property-probe.h \
	oss4-sink.h           \
	oss4-soundcard.h      \
	oss4-source.h

