plugin_LTLIBRARIES = libgstjpegformat.la

libgstjpegformat_la_SOURCES = gstjpegformat.c gstjpegparse.c gstjifmux.c
libgstjpegformat_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstjpegformat_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_API_VERSION@ $(GST_BASE_LIBS) $(GST_LIBS)
libgstjpegformat_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstjpegformat_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstjpegformat.h gstjpegparse.h gstjifmux.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstjpegformat -:SHARED libgstjpegformat \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstjpegformat_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstjpegformat_la_CFLAGS) \
	 -:LDFLAGS $(libgstjpegformat_la_LDFLAGS) \
	           $(libgstjpegformat_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
