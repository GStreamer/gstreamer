plugin_LTLIBRARIES = libgstfaad.la

libgstfaad_la_SOURCES = gstfaad.c
libgstfaad_la_CFLAGS = -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(FAAD_CFLAGS)
libgstfaad_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(FAAD_LIBS)
libgstfaad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfaad_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstfaad.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstfaad -:SHARED libgstfaad \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstfaad_la_SOURCES) \
	 -:CPPFLAGS $(CPPFLAGS) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstfaad_la_CFLAGS) \
	 -:LDFLAGS $(libgstfaad_la_LDFLAGS) \
	           $(libgstfaad_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
