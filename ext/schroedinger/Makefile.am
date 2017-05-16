
plugin_LTLIBRARIES = libgstschro.la

noinst_HEADERS = \
	gstschroutils.h

libgstschro_la_SOURCES = \
	gstschro.c \
	gstschrodec.c \
	gstschroenc.c \
	gstschroutils.c
libgstschro_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
        $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(SCHRO_CFLAGS)
libgstschro_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
        $(GST_BASE_LIBS) $(GST_LIBS) \
	$(SCHRO_LIBS)
libgstschro_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)


