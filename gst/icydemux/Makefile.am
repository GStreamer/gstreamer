plugin_LTLIBRARIES = libgsticydemux.la

libgsticydemux_la_SOURCES = gsticydemux.c
libgsticydemux_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgsticydemux_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_API_VERSION@ $(GST_BASE_LIBS) $(GST_LIBS) $(ZLIB_LIBS)
libgsticydemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsticydemux_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsticydemux.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgsticydemux -:SHARED libgsticydemux \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgsticydemux_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgsticydemux_la_CFLAGS) \
	 -:LDFLAGS $(libgsticydemux_la_LDFLAGS) \
	           $(libgsticydemux_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
