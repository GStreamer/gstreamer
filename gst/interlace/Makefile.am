plugin_LTLIBRARIES = libgstinterlace.la

libgstinterlace_la_SOURCES = \
	gstinterlace.c

libgstinterlace_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)

libgstinterlace_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(LIBM)

libgstinterlace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstinterlace_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstinterlace -:SHARED libgstinterlace \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstinterlace_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstinterlace_la_CFLAGS) \
	 -:LDFLAGS $(libgstinterlace_la_LDFLAGS) \
	           $(libgstinterlace_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@