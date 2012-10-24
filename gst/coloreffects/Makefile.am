plugin_LTLIBRARIES = libgstcoloreffects.la

libgstcoloreffects_la_SOURCES = \
	gstplugin.c \
	gstcoloreffects.c \
	gstchromahold.c
libgstcoloreffects_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstcoloreffects_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstcoloreffects_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcoloreffects_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstcoloreffects.h gstchromahold.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstcoloreffects -:SHARED libgstcoloreffects \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstcoloreffects_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstcoloreffects_la_CFLAGS) \
	 -:LDFLAGS $(libgstcoloreffects_la_LDFLAGS) \
	           $(libgstcoloreffects_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
