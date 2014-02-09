
plugin_LTLIBRARIES = libgstfragmented.la

libgstfragmented_la_SOURCES =			\
	m3u8.c					\
	gsthlsdemux.c				\
	gstfragmentedplugin.c 			\
	gsthlssink.c 				\
	gstm3u8playlist.c

libgstfragmented_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS) $(LIBGCRYPT_CFLAGS) $(NETTLE_CFLAGS)
libgstfragmented_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/uridownloader/libgsturidownloader-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgstpbutils-$(GST_API_VERSION) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(GIO_LIBS) $(LIBM) $(LIBGCRYPT_LIBS) $(NETTLE_LIBS)
libgstfragmented_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -no-undefined
libgstfragmented_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = 			\
	gstfragmented.h			\
	gsthlsdemux.h			\
	gsthlssink.h			\
	gstm3u8playlist.h		\
	m3u8.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgsthls -:SHARED libgsthls \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgsthls_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgsthls_la_CFLAGS) \
	 -:LDFLAGS $(libgsthls_la_LDFLAGS) \
	           $(libgsthls_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
