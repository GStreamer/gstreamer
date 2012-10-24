plugin_LTLIBRARIES = libgstdvbsuboverlay.la

libgstdvbsuboverlay_la_SOURCES = dvb-sub.c gstdvbsuboverlay.c

libgstdvbsuboverlay_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstdvbsuboverlay_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ $(GST_LIBS)
libgstdvbsuboverlay_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdvbsuboverlay_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstdvbsuboverlay.h dvb-sub.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstdvbsuboverlay -:SHARED libgstdvbsuboverlay \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstdvbsuboverlay_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstdvbsuboverlay_la_CFLAGS) \
	 -:LDFLAGS $(libgstdvbsuboverlay_la_LDFLAGS) \
	           $(libgstdvbsuboverlay_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
