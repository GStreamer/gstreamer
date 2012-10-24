plugin_LTLIBRARIES = libgstvideocrop.la

# Note: we only use defines from gst/video/video.h, but none
# of the functions, so we don't need to link to libgstvideo

libgstvideocrop_la_SOURCES = gstvideocrop.c gstaspectratiocrop.c
libgstvideocrop_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstvideocrop_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION)
libgstvideocrop_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideocrop_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstvideocrop.h gstaspectratiocrop.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstvideocrop -:SHARED libgstvideocrop \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvideocrop_la_SOURCES) \
	 	   $(nodist_libgstvideocrop_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvideocrop_la_CFLAGS) \
	 -:LDFLAGS $(libgstvideocrop_la_LDFLAGS) \
	           $(libgstvideocrop_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
