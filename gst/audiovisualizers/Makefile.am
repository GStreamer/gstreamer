plugin_LTLIBRARIES = libgstaudiovisualizers.la

libgstaudiovisualizers_la_SOURCES = plugin.c \
    gstbaseaudiovisualizer.c gstbaseaudiovisualizer.h \
    gstspacescope.c gstspacescope.h \
    gstspectrascope.c gstspectrascope.h \
    gstsynaescope.c gstsynaescope.h \
    gstwavescope.c gstwavescope.h

libgstaudiovisualizers_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CONTROLLER_CFLAGS) $(GST_CFLAGS)
libgstaudiovisualizers_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) \
	-lgstvideo-$(GST_MAJORMINOR) -lgstfft-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS)  $(GST_CONTROLLER_LIBS) $(GST_LIBS) $(LIBM)
libgstaudiovisualizers_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiovisualizers_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstbaseaudiovisualizer.h gstdrawhelpers.h \
	gstspacescope.h gstspectrascope.h gstsynaescope.h gstwavescope.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT audiovisualizers -:SHARED audiovisualizers \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstaudiovisualizers_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstaudiovisualizers_la_CFLAGS) \
	 -:LDFLAGS $(libgstaudiovisualizers_la_LDFLAGS) \
	           $(libgstaudiovisualizers_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@

