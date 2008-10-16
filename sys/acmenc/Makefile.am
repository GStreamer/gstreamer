plugin_LTLIBRARIES = libgstacmencsink.la

libgstacmencsink_la_SOURCES = acmenc.c
libgstacmencsink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstacmencsink_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstriff-$(GST_MAJORMINOR) -lmsacm32
libgstacmencsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

