plugin_LTLIBRARIES = libgstivtc.la

libgstivtc_la_SOURCES = \
	gstivtc.c gstivtc.h \
	gstcombdetect.c gstcombdetect.h
libgstivtc_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstivtc_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-1.0 \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstivtc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstivtc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)


EXTRA_DIST =

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstivtc -:SHARED libgstivtc \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstivtc_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstivtc_la_CFLAGS) \
	 -:LDFLAGS $(libgstivtc_la_LDFLAGS) \
	           $(libgstivtc_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
