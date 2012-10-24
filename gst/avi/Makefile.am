plugin_LTLIBRARIES = libgstavi.la

libgstavi_la_SOURCES = \
	gstavi.c \
	gstavimux.c \
	gstavidemux.c \
	gstavisubtitle.c

noinst_HEADERS = \
	avi-ids.h \
	gstavimux.h \
	gstavidemux.h \
	gstavisubtitle.h

libgstavi_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstavi_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	-lgstriff-@GST_API_VERSION@ \
	-lgstaudio-@GST_API_VERSION@ \
	-lgsttag-@GST_API_VERSION@ \
	-lgstvideo-@GST_API_VERSION@

libgstavi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstavi_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

EXTRA_DIST = README

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstavi -:SHARED libgstavi \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstavi_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstavi_la_CFLAGS) \
	 -:LDFLAGS $(libgstavi_la_LDFLAGS) \
	           $(libgstavi_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
