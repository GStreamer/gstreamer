
plugin_LTLIBRARIES = libgststereo.la

libgststereo_la_SOURCES = gststereo.c
libgststereo_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
libgststereo_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION)
libgststereo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgststereo_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gststereo.h

EXTRA_DIST =

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgststereo -:SHARED libgststereo \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgststereo_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgststereo_la_CFLAGS) \
	 -:LDFLAGS $(libgststereo_la_LDFLAGS) \
	           $(libgststereo_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
