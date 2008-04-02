plugin_LTLIBRARIES = libgstoss4audio.la

libgstoss4audio_la_SOURCES = \
	oss4-audio.c          \
	oss4-mixer.c          \
	oss4-mixer-enum.c     \
	oss4-mixer-slider.c   \
	oss4-mixer-switch.c   \
	oss4-property-probe.c \
	oss4-sink.c           \
	oss4-source.c

libgstoss4audio_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstoss4audio_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
        -lgstinterfaces-$(GST_MAJORMINOR) \
	-lgstaudio-$(GST_MAJORMINOR) \
	$(GST_LIBS)
libgstoss4audio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 

noinst_HEADERS = \
	oss4-audio.h          \
	oss4-mixer.h          \
	oss4-mixer-enum.h     \
	oss4-mixer-slider.h   \
	oss4-mixer-switch.h   \
	oss4-property-probe.h \
	oss4-sink.h           \
	oss4-soundcard.h      \
	oss4-source.h


