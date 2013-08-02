plugin_LTLIBRARIES = libgstmpegpsdemux.la

libgstmpegpsdemux_la_SOURCES = \
	plugin.c \
	gstmpegdemux.c \
	gstpesfilter.c 

libgstmpegpsdemux_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegpsdemux_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegpsdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegpsdemux_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstmpegdefs.h   \
	gstmpegdemux.h  \
	gstpesfilter.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmpegpsdemux -:SHARED libgstmpegpsdemux \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmpegpsdemux_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmpegpsdemux_la_CFLAGS) \
	 -:LDFLAGS $(libgstmpegpsdemux_la_LDFLAGS) \
	           $(libgstmpegpsdemux_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
