
plugin_LTLIBRARIES = libgsthls.la

libgsthls_la_SOURCES =			\
	m3u8.c					\
	gsthlsdemux.c				\
	gsthlsdemux-util.c  \
	gsthlsplugin.c 			\
	gsthlssink.c 				\
	gsthlssink2.c 				\
	gstm3u8playlist.c

libgsthls_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(LIBGCRYPT_CFLAGS) $(NETTLE_CFLAGS) $(OPENSSL_CFLAGS)
libgsthls_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/uridownloader/libgsturidownloader-@GST_API_VERSION@.la \
        $(top_builddir)/gst-libs/gst/adaptivedemux/libgstadaptivedemux-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgstpbutils-$(GST_API_VERSION) -lgstvideo-$(GST_API_VERSION) -lgsttag-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM) $(LIBGCRYPT_LIBS) $(NETTLE_LIBS) $(OPENSSL_LIBS)
libgsthls_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -no-undefined

# headers we need but don't want installed
noinst_HEADERS = 			\
	gsthls.h			\
	gsthlsdemux.h			\
	gsthlssink.h			\
	gsthlssink2.h			\
	gstm3u8playlist.h		\
	m3u8.h
