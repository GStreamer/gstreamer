plugin_LTLIBRARIES = libgstsegmentclip.la

libgstsegmentclip_la_SOURCES = gstsegmentclip.c gstaudiosegmentclip.c gstvideosegmentclip.c plugin.c

libgstsegmentclip_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstsegmentclip_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) -lgstaudio-@GST_API_VERSION@
libgstsegmentclip_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsegmentclip_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstsegmentclip.h gstaudiosegmentclip.h gstvideosegmentclip.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstsegmentclip -:SHARED libgstsegmentclip \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstsegmentclip_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstsegmentclip_la_CFLAGS) \
	 -:LDFLAGS $(libgstsegmentclip_la_LDFLAGS) \
	           $(libgstsegmentclip_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@