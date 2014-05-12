
plugin_LTLIBRARIES = libgstsmoothstreaming.la

libgstsmoothstreaming_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
    $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
    -DGST_USE_UNSTABLE_API $(LIBXML2_CFLAGS)
libgstsmoothstreaming_la_LIBADD = \
    $(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-$(GST_API_VERSION).la \
    $(top_builddir)/gst-libs/gst/uridownloader/libgsturidownloader-$(GST_API_VERSION).la \
    $(GST_PLUGINS_BASE_LIBS) \
    -lgsttag-$(GST_API_VERSION) \
    $(GST_BASE_LIBS) $(GST_LIBS) $(ZLIB_LIBS) $(LIBXML2_LIBS)
libgstsmoothstreaming_la_LDFLAGS = ${GST_PLUGIN_LDFLAGS}
libgstsmoothstreaming_la_SOURCES = gstsmoothstreaming-plugin.c \
	gstmssdemux.c \
	gstmssmanifest.c
libgstsmoothstreaming_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstmssdemux.h \
	gstmssmanifest.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstsmoothstreaming -:SHARED libgstsmoothstreaming \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstsmoothstreaming_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(CPPFLAGS) $(libgstsmoothstreaming_la_CFLAGS) \
	 -:LDFLAGS $(libgstsmoothstreaming_la_LDFLAGS) \
	           $(libgstsmoothstreaming_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@

