plugin_LTLIBRARIES = libgstshapewipe.la

libgstshapewipe_la_SOURCES = gstshapewipe.c

libgstshapewipe_la_CFLAGS = $(GIO_CFLAGS) $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstshapewipe_la_LIBADD = $(GIO_LIBS) $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@
libgstshapewipe_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstshapewipe_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstshapewipe.h


Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstshapewhipe -:SHARED libgstshapewhipe \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstshapewhipe_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstshapewhipe_la_CFLAGS) \
	 -:LDFLAGS $(libgstshapewhipe_la_LDFLAGS) \
	           $(libgstshapewhipe_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
