plugin_LTLIBRARIES = libgstjp2kdecimator.la

libgstjp2kdecimator_la_SOURCES = \
	gstjp2kdecimator.c \
	jp2kcodestream.c

libgstjp2kdecimator_la_CFLAGS = \
    $(GST_CFLAGS)
libgstjp2kdecimator_la_LIBADD = \
    $(GST_LIBS) $(GST_BASE_LIBS)
libgstjp2kdecimator_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstjp2kdecimator_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstjp2kdecimator.h jp2kcodestream.h
