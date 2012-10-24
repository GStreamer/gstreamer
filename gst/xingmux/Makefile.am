# FIXME 0.11: element should move somewhere else really, such as
# gst-plugins-good/gst/tags/ or so
plugin_LTLIBRARIES = libgstxingmux.la

libgstxingmux_la_SOURCES = plugin.c gstxingmux.c
libgstxingmux_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstxingmux_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstxingmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstxingmux_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstxingmux.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstxingmux -:SHARED libgstxingmux \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstxingmux_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstxingmux_la_CFLAGS) \
	 -:LDFLAGS $(libgstxingmux_la_LDFLAGS) \
	           $(libgstxingmux_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
