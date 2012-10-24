
plugin_LTLIBRARIES = libgstnuvdemux.la

libgstnuvdemux_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS)
libgstnuvdemux_la_LIBADD = $(GST_BASE_LIBS)
libgstnuvdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstnuvdemux_la_SOURCES = gstnuvdemux.c
libgstnuvdemux_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstnuvdemux.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstnuvdemux -:SHARED libgstnuvdemux \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstnuvdemux_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstnuvdemux_la_CFLAGS) \
	 -:LDFLAGS $(libgstnuvdemux_la_LDFLAGS) \
	           $(libgstnuvdemux_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
