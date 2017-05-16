
plugin_LTLIBRARIES = libgstdvdlpcmdec.la

libgstdvdlpcmdec_la_SOURCES = gstdvdlpcmdec.c
libgstdvdlpcmdec_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstdvdlpcmdec_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ $(GST_BASE_LIBS) $(GST_LIBS)
libgstdvdlpcmdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdvdlpcmdec.h
