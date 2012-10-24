plugin_LTLIBRARIES = libgstcutter.la

libgstcutter_la_SOURCES = gstcutter.c
libgstcutter_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstcutter_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LIBM)
libgstcutter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcutter_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstcutter.h filter.func

EXTRA_DIST = README

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstcutter -:SHARED libgstcutter \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstcutter_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstcutter_la_CFLAGS) \
	 -:LDFLAGS $(libgstcutter_la_LDFLAGS) \
	           $(libgstcutter_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
