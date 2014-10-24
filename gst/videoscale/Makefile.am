plugin_LTLIBRARIES = libgstvideoscale.la

libgstvideoscale_la_SOURCES = gstvideoscale.c 

libgstvideoscale_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstvideoscale_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideoscale_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstvideoscale_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstvideoscale.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstvideoscale -:SHARED libgstvideoscale \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvideoscale_la_SOURCES)
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvideoscale_la_CFLAGS) \
	 -:LDFLAGS $(libgstvideoscale_la_LDFLAGS) \
	           $(libgstvideoscale_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
