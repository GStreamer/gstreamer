plugin_LTLIBRARIES = libgstgme.la

libgstgme_la_SOURCES = gstgme.c

libgstgme_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GME_CFLAGS)
libgstgme_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(GME_LIBS)
libgstgme_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstgme.h
