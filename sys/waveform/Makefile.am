plugin_LTLIBRARIES = libgstwaveformsink.la

libgstwaveformsink_la_SOURCES =  gstwaveformsink.c gstwaveformplugin.c
libgstwaveformsink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
        $(GST_PLUGINS_BASE_CFLAGS)
libgstwaveformsink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
        -lgstaudio-$(GST_API_VERSION) \
        $(GST_BASE_LIBS) \
	$(GST_LIBS) \
	-lwinmm
libgstwaveformsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwaveformsink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstwaveformsink.h

