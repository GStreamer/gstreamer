plugin_LTLIBRARIES = libgstacmdecsink.la

libgstacmdecsink_la_SOURCES = acmmp3dec.c
libgstacmdecsink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstacmdecsink_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstriff-$(GST_MAJORMINOR) -lmsacm32
libgstacmdecsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DIRECTSOUND_LDFLAGS)

