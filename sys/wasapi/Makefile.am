plugin_LTLIBRARIES = libgstwasapi.la

libgstwasapi_la_SOURCES = gstwasapi.c \
	gstwasapisrc.c \
	gstwasapisink.c \
	gstwasapiutil.c 

libgstwasapi_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) -DCOBJMACROS=1
libgstwasapi_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
        $(GST_BASE_LIBS) $(GST_LIBS) \
	$(WASAPI_LIBS)
libgstwasapi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 

noinst_HEADERS = gstwasapisrc.h \
	gstwasapisink.h \
	gstwasapiutil.h

