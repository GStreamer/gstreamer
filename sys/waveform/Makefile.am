plugin_LTLIBRARIES = libgstwaveform.la

libgstwaveform_la_SOURCES =  gstwaveformsink.c gstwaveformplugin.c
libgstwaveform_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
        $(GST_PLUGINS_BASE_CFLAGS)
libgstwaveform_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
        -lgstaudio-$(GST_API_VERSION) \
        $(GST_BASE_LIBS) \
	$(GST_LIBS) \
	-lwinmm
libgstwaveform_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstwaveformsink.h

