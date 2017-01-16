plugin_LTLIBRARIES = libgstaudiomixmatrix.la

libgstaudiomixmatrix_la_SOURCES = gstaudiomixmatrix.c

libgstaudiomixmatrix_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstaudiomixmatrix_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstaudiomixmatrix_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiomixmatrix_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstaudiomixmatrix.h
