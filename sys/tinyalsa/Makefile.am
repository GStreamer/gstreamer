
plugin_LTLIBRARIES = libgsttinyalsa.la

libgsttinyalsa_la_SOURCES = tinyalsasink.c 	\
                            tinyalsa.c

libgsttinyalsa_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
    -Wno-deprecated-declarations
libgsttinyalsa_la_LIBADD =            \
    -ltinyalsa                        \
    -lgstaudio-@GST_API_VERSION@      \
    $(GST_PLUGINS_BASE_LIBS)          \
    $(GST_BASE_LIBS)                  \
    $(GST_LIBS)
libgsttinyalsa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttinyalsa_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = tinyalsasink.h

