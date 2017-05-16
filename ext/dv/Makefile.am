plugin_LTLIBRARIES = libgstdv.la

libgstdv_la_SOURCES = gstdv.c gstdvdec.c gstdvdemux.c gstsmptetimecode.c
libgstdv_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(LIBDV_CFLAGS)
libgstdv_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBDV_LIBS)
libgstdv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdvdemux.h gstdvdec.h gstsmptetimecode.h

EXTRA_DIST = NOTES

noinst_PROGRAMS = smpte_test

smpte_test_SOURCES = smpte_test.c gstsmptetimecode.c
smpte_test_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(LIBDV_CFLAGS)
smpte_test_LDADD = $(GST_BASE_LIBS) $(GST_LIBS)

