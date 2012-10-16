plugin_LTLIBRARIES = libgstmedian.la

libgstmedian_la_SOURCES = gstmedian.c
libgstmedian_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmedian_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstmedian_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmedian_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstmedian.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmedian -:SHARED libgstmedian \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmedian_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmedian_la_CFLAGS) \
	 -:LDFLAGS $(libgstmedian_la_LDFLAGS) \
	           $(libgstmedian_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-1.0' \
	> $@
