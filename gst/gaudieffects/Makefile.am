plugin_LTLIBRARIES = libgstgaudieffects.la

libgstgaudieffects_la_SOURCES = gstburn.c gstchromium.c gstdilate.c \
        gstdodge.c gstexclusion.c gstgaussblur.c gstsolarize.c gstplugin.c
libgstgaudieffects_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS) $(GST_CFLAGS)
libgstgaudieffects_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ $(GST_CONTROLLER_LIBS) $(GST_LIBS) $(LIBM)
libgstgaudieffects_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgaudieffects_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = \
        gstburn.h gstchromium.h gstdilate.h gstdodge.h \
        gstexclusion.h gstgaussblur.h gstplugin.h gstsolarize.h

EXTRA_DIST = blur-example.py  burn-example.py

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstgaudieffects -:SHARED libgstgaudieffects \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstgaudieffects_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstgaudieffects_la_CFLAGS) \
	 -:LDFLAGS $(libgstgaudieffects_la_LDFLAGS) \
	           $(libgstgaudieffects_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
