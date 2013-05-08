
plugin_LTLIBRARIES = libgstdashdemux.la

libgstdashdemux_la_SOURCES =			\
	gstmpdparser.c				\
	gstdashdemux.c				\
	gstfragment.c				\
	gsturidownloader.c			\
	gstplugin.c

# headers we need but don't want installed
noinst_HEADERS =             \
        gstmpdparser.h			\
	gstfragmented.h			\
	gstfragment.h			\
	gstdashdemux.h			\
	gsturidownloader.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstdashdemux_la_CFLAGS = $(GST_CFLAGS) 
libgstdashdemux_la_LIBADD = $(GST_LIBS)
libgstdashdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdashdemux_la_LIBTOOLFLAGS = --tag=disable-static

