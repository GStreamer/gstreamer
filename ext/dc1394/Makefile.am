plugin_LTLIBRARIES = \
    libgstdc1394.la

libgstdc1394_la_SOURCES = \
    gstdc1394src.c
libgstdc1394_la_CFLAGS = \
    $(LIBDC1394_CFLAGS) \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_BASE_CFLAGS) \
    $(GST_CFLAGS)
libgstdc1394_la_LIBADD = \
    $(LIBDC1394_LIBS) \
    $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
    $(GST_BASE_LIBS) \
    $(GST_LIBS)
libgstdc1394_la_LDFLAGS = \
    $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
    gstdc1394src.h
