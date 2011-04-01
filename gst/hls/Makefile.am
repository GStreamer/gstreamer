
plugin_LTLIBRARIES = libgstfragmented.la

libgstfragmented_la_SOURCES =			\
	m3u8.c					\
	gsthlsdemux.c				\
	gstfragmentedplugin.c

libgstfragmented_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(SOUP_CFLAGS)
libgstfragmented_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(SOUP_LIBS)
libgstfragmented_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -no-undefined
libgstfragmented_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = 			\
	gstfragmented.h		\
	gsthlsdemux.h			\
	m3u8.h
