plugin_LTLIBRARIES = libgstdvdsub.la

libgstdvdsub_la_SOURCES = gstdvdsubdec.c gstdvdsubparse.c
libgstdvdsub_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstdvdsub_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstdvdsub_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdvdsub_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstdvdsubdec.h gstdvdsubparse.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstdvdsub -:SHARED libgstdvdsub \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstdvdsub_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstdvdsub_la_CFLAGS) \
	 -:LDFLAGS $(libgstdvdsub_la_LDFLAGS) \
	           $(libgstdvdsub_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
