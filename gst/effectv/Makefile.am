plugin_LTLIBRARIES = libgsteffectv.la

libgsteffectv_la_SOURCES = \
	gsteffectv.c gstedge.c gstaging.c gstdice.c gstwarp.c \
	gstshagadelic.c gstvertigo.c gstrev.c gstquark.c gstop.c \
	gstradioac.c gststreak.c gstripple.c
libgsteffectv_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst/videofilter
libgsteffectv_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM)
libgsteffectv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsteffectv_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsteffectv.h gstaging.h gstdice.h gstedge.h \
        gstquark.h gstrev.h gstshagadelic.h gstvertigo.h gstwarp.h gstop.h \
	gstradioac.h gststreak.h gstripple.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgsteffectv -:SHARED libgsteffectv \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgsteffectv_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgsteffectv_la_CFLAGS) \
	 -:LDFLAGS $(libgsteffectv_la_LDFLAGS) \
	           $(libgsteffectv_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
