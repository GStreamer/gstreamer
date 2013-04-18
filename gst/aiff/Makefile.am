plugin_LTLIBRARIES = libgstaiff.la

libgstaiff_la_SOURCES = aiff.c aiffmux.c aiffparse.c
libgstaiff_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstaiff_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) -lgstaudio-$(GST_API_VERSION)\
	$(GST_BASE_LIBS) \
	$(LIBM)
libgstaiff_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaiff_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = aiffmux.h aiffparse.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstaiff -:SHARED libgstaiff \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstaiff_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstaiff_la_CFLAGS) \
	 -:LDFLAGS $(libgstaiff_la_LDFLAGS) \
	           $(libgstaiff_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
