plugin_LTLIBRARIES = libgstflv.la

libgstflv_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstflv_la_LIBADD = -lgstpbutils-@GST_API_VERSION@ -lgstaudio-@GST_API_VERSION@\
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)
libgstflv_la_LDFLAGS = ${GST_PLUGIN_LDFLAGS}
libgstflv_la_SOURCES = gstflvdemux.c gstflvmux.c
libgstflv_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstflvdemux.h gstflvmux.h amfdefs.h gstindex.h

EXTRA_DIST = gstindex.c gstmemindex.c

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstflv -:SHARED libgstflv \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstflv_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstflv_la_CFLAGS) \
	 -:LDFLAGS $(libgstflv_la_LDFLAGS) \
	           $(libgstflv_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
