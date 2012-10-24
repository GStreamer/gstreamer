plugin_LTLIBRARIES = libgstsdpelem.la

libgstsdpelem_la_SOURCES = gstsdpelem.c gstsdpdemux.h gstsdpdemux.c

libgstsdpelem_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
libgstsdpelem_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GIO_LIBS) \
		       -lgstrtp-@GST_API_VERSION@ -lgstsdp-@GST_API_VERSION@ \
		       $(GST_LIBS) $(WINSOCK2_LIBS)
libgstsdpelem_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsdpelem_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstsdpdemux.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstsdp -:SHARED libgstsdp \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstsdp_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstsdp_la_CFLAGS) \
	 -:LDFLAGS $(libgstsdp_la_LDFLAGS) \
	           $(libgstsdp_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
