plugin_LTLIBRARIES = libgstrtpmux.la

libgstrtpmux_la_SOURCES = gstrtpmuxer.c gstrtpmux.c gstrtpdtmfmux.c

libgstrtpmux_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	-DEXTERN_BUF -DRTP_SUPPORT
libgstrtpmux_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstrtpmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtpmux_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstrtpmux.h gstrtpdtmfmux.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstrtpmux -:SHARED libgstrtpmux \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstrtpmux_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstrtpmux_la_CFLAGS) \
	 -:LDFLAGS $(libgstrtpmux_la_LDFLAGS) \
	           $(libgstrtpmux_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@