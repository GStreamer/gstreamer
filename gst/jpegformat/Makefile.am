plugin_LTLIBRARIES = libgstjpegformat.la

libgstjpegformat_la_SOURCES = gstjpegformat.c gstjpegparse.c gstjifmux.c
libgstjpegformat_la_CFLAGS = \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstjpegformat_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-@GST_MAJORMINOR@ \
    -lgsttag-@GST_MAJORMINOR@ $(GST_LIBS) $(GST_BASE_LIBS)
libgstjpegformat_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstjpegformat_la_LIBTOOLFLAGS = --tag=disable-static

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
