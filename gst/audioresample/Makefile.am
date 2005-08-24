
plugin_LTLIBRARIES = libgstaudioresample.la

resample_SOURCES = \
	functable.c \
	functable.h \
	resample.c \
	resample_functable.c \
	resample_ref.c \
	resample_chunk.c \
	resample.h \
	debug.c \
	debug.h \
	buffer.c \
	buffer.h

libgstaudioresample_la_SOURCES = gstaudioresample.c $(resample_SOURCES)
libgstaudioresample_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(LIBOIL_CFLAGS)
libgstaudioresample_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(LIBOIL_LIBS)
libgstaudioresample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

