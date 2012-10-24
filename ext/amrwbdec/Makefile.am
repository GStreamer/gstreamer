plugin_LTLIBRARIES = libgstamrwbdec.la

libgstamrwbdec_la_SOURCES = \
	amrwb.c \
	amrwbdec.c

libgstamrwbdec_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AMRWB_CFLAGS)
libgstamrwbdec_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	-lgstaudio-@GST_API_VERSION@ \
	$(GST_LIBS) $(AMRWB_LIBS)
libgstamrwbdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstamrwbdec_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	amrwbdec.h
