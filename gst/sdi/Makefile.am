plugin_LTLIBRARIES = libgstsdi.la

libgstsdi_la_SOURCES = gstsdi.c \
	gstsdidemux.c \
	gstsdimux.c

libgstsdi_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstsdi_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
		       $(GST_LIBS)
libgstsdi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsdi_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstsdidemux.h gstsdimux.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstsdi -:SHARED libgstsdi \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstsdi_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstsdi_la_CFLAGS) \
	 -:LDFLAGS $(libgstsdi_la_LDFLAGS) \
	           $(libgstsdi_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@