plugin_LTLIBRARIES = libgstspeexresample.la

libgstspeexresample_la_SOURCES = \
	gstspeexresample.c \
	speex_resampler_int.c \
	speex_resampler_float.c \
	speex_resampler_double.c

libgstspeexresample_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstspeexresample_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	-lgstaudio-$(GST_MAJORMINOR) \
	$(LIBM)

libgstspeexresample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	arch.h \
	fixed_generic.h \
	gstspeexresample.h \
	speex_resampler.h \
	speex_resampler_wrapper.h

EXTRA_DIST = \
	resample.c

