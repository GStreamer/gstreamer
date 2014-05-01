
plugin_LTLIBRARIES = libgstdashdemux.la

libgstdashdemux_la_SOURCES =			\
	gstmpdparser.c				\
	gstdashdemux.c				\
	gstplugin.c

# headers we need but don't want installed
noinst_HEADERS =        \
        gstmpdparser.h	\
	gstdashdemux.h	\
	gstdash_debug.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstdashdemux_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS) $(LIBXML2_CFLAGS)
libgstdashdemux_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/uridownloader/libgsturidownloader-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GST_BASE_LIBS) $(LIBXML2_LIBS)
libgstdashdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdashdemux_la_LIBTOOLFLAGS = --tag=disable-static

