
plugin_LTLIBRARIES = libgstsmooth.la

libgstsmooth_la_SOURCES = gstsmooth.c
libgstsmooth_la_CFLAGS = \
  $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstsmooth_la_LIBADD =  \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstsmooth_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsmooth_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstsmooth.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstsmooth -:SHARED libgstsmooth \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstsmooth_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstsmooth_la_CFLAGS) \
	 -:LDFLAGS $(libgstsmooth_la_LDFLAGS) \
	           $(libgstsmooth_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
