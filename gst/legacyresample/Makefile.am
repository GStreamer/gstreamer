plugin_LTLIBRARIES = libgstlegacyresample.la

resample_SOURCES = \
	functable.c \
	resample.c \
	resample_functable.c \
	resample_ref.c \
	resample.h \
	buffer.c

noinst_HEADERS = \
	gstlegacyresample.h \
	functable.h \
	debug.h \
	buffer.h

libgstlegacyresample_la_SOURCES = gstlegacyresample.c $(resample_SOURCES)
libgstlegacyresample_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(LIBOIL_CFLAGS)
libgstlegacyresample_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(LIBOIL_LIBS)
libgstlegacyresample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstlegacyresample_la_LIBTOOLFLAGS = --tag=disable-static

