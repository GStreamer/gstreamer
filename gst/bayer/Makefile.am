plugin_LTLIBRARIES = libgstbayer.la

libgstbayer_la_SOURCES = \
	gstbayer.c \
	gstbayer2rgb.c \
	gstrgb2bayer.c \
	gstrgb2bayer.h
libgstbayer_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
    $(GST_CFLAGS)
libgstbayer_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
    $(GST_BASE_LIBS)
libgstbayer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstbayer_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstbayer -:SHARED libgstbayer \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstbayer_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstbayer_la_CFLAGS) \
	 -:LDFLAGS $(libgstbayer_la_LDFLAGS) \
	           $(libgstbayer_la_LIBADD) \
	           $(libgstbayer_la_LIBTOOLFLAGS) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
