plugin_LTLIBRARIES = libgstautoconvert.la

libgstautoconvert_la_SOURCES = gstautoconvert.c gstautovideoconvert.c plugin.c

libgstautoconvert_la_CFLAGS = $(GST_CFLAGS)
libgstautoconvert_la_LIBADD = $(GST_LIBS)
libgstautoconvert_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstautoconvert_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstautoconvert.h gstautovideoconvert.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstautoconvert -:SHARED libgstautoconvert \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstautoconvert_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstautoconvert_la_CFLAGS) \
	 -:LDFLAGS $(libgstautoconvert_la_LDFLAGS) \
	           $(libgstautoconvert_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
