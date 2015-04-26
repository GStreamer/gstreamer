plugin_LTLIBRARIES = libgstautodetect.la

libgstautodetect_la_SOURCES = \
	gstautoaudiosink.c \
	gstautoaudiosrc.c \
	gstautodetect.c \
	gstautovideosink.c \
	gstautovideosrc.c
libgstautodetect_la_CFLAGS = $(GST_CFLAGS)
libgstautodetect_la_LIBADD = $(GST_LIBS)
libgstautodetect_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstautodetect_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstautoaudiosink.h \
	gstautoaudiosrc.h \
	gstautodetect.h \
	gstautovideosink.h \
	gstautovideosrc.h
