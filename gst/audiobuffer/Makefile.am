plugin_LTLIBRARIES = libgstaudiobuffer.la

libgstaudiobuffer_la_SOURCES = gstaudioringbuffer.c
libgstaudiobuffer_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
    $(GST_CFLAGS) 
libgstaudiobuffer_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
			   -lgstaudio-$(GST_MAJORMINOR)

libgstaudiobuffer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiobuffer_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstaudiobuffer -:SHARED libgstaudiobuffer \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstaudiobuffer_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstaudiobuffer_la_CFLAGS) \
	 -:LDFLAGS $(libgstaudiobuffer_la_LDFLAGS) \
	           $(libgstaudiobuffer_la_LIBADD) \
	           $(libgstaudiobuffer_la_LIBTOOLFLAGS) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
