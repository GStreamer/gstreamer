plugin_LTLIBRARIES = libgstvideomeasure.la 

noinst_HEADERS = gstvideomeasure_ssim.h gstvideomeasure_collector.h

libgstvideomeasure_la_SOURCES = \
    gstvideomeasure.c \
    gstvideomeasure.h \
    gstvideomeasure_ssim.c \
    gstvideomeasure_collector.c

libgstvideomeasure_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_BASE_CFLAGS) \
    $(GST_CFLAGS)
libgstvideomeasure_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
    -lgstvideo-@GST_API_VERSION@ $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstvideomeasure_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideomeasure_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
