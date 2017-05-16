plugin_LTLIBRARIES = libgstofa.la

libgstofa_la_SOURCES = gstofa.c

libgstofa_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(OFA_CFLAGS) 

libgstofa_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(OFA_LIBS)

libgstofa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstofa.h
