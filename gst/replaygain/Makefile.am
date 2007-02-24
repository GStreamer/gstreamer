plugin_LTLIBRARIES = libgstreplaygain.la

libgstreplaygain_la_SOURCES = \
	gstrganalysis.c       \
	rganalysis.c
libgstreplaygain_la_CFLAGS  = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstreplaygain_la_LIBADD  = $(GST_LIBS) $(GST_BASE_LIBS) $(LIBM)
libgstreplaygain_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS =         \
	gstrganalysis.h  \
	rganalysis.h
