plugin_LTLIBRARIES = libgstgme.la

libgstgme_la_SOURCES = gstgme.c tag.c

libgstgme_la_CFLAGS = $(GST_CFLAGS) $(GME_CFLAGS)
libgstgme_la_LIBADD = $(GST_LIBS) $(GME_LIBS)
libgstgme_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgme_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstgme.h tag.h
