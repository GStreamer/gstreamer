plugin_LTLIBRARIES = libgstaudiofxbad.la

libgstaudiofxbad_la_SOURCES = gstaudiofxbad.c \
    gstaudiochannelmix.c gstaudiochannelmix.h

libgstaudiofxbad_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstaudiofxbad_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS)  $(GST_LIBS) $(LIBM)
libgstaudiofxbad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiofxbad_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT audiofxbad -:SHARED audiofxbad \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstaudiofxbad_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstaudiofxbad_la_CFLAGS) \
	 -:LDFLAGS $(libgstaudiofxbad_la_LDFLAGS) \
	           $(libgstaudiofxbad_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-1.0' \
	> $@

