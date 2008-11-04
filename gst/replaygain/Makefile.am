plugin_LTLIBRARIES = libgstreplaygain.la

libgstreplaygain_la_SOURCES = \
	gstrganalysis.c       \
	gstrglimiter.c        \
	gstrgvolume.c         \
	replaygain.c          \
	rganalysis.c
libgstreplaygain_la_CFLAGS  = \
	$(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstreplaygain_la_LIBADD  = \
	$(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstpbutils-0.10 $(LIBM)
libgstreplaygain_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstreplaygain_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS =         \
	gstrganalysis.h  \
	gstrglimiter.h   \
	gstrgvolume.h    \
	replaygain.h     \
	rganalysis.h
