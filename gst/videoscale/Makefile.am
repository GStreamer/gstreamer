plugin_LTLIBRARIES = libgstvideoscale.la

libgstvideoscale_la_SOURCES = \
	gstvideoscale.c \
	vs_image.c \
	vs_scanline.c

libgstvideoscale_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstvideoscale_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideoscale_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)

noinst_HEADERS = \
	gstvideoscale.h \
	vs_image.h \
	vs_scanline.h
