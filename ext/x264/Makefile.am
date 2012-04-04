plugin_LTLIBRARIES = libgstx264.la

libgstx264_la_SOURCES = gstx264enc.c
libgstx264_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(X264_CFLAGS)
libgstx264_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GST_LIBS) \
	$(X264_LIBS)
libgstx264_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstx264_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstx264enc.h

presetdir = $(datadir)/gstreamer-$(GST_API_VERSION)/presets
preset_DATA = GstX264Enc.prs

EXTRA_DIST = $(preset_DATA)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstx264 -:SHARED libgstx264 \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstx264_la_SOURCES) \
	 -:CPPFLAGS $(CPPFLAGS) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstx264_la_CFLAGS) -I'$$(GSTREAMER_AGGREGATE_TOP)/x264' \
	 -:LDFLAGS $(libgstx264_la_LDFLAGS) \
	           $(libgstx264_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
