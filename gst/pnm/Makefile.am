plugin_LTLIBRARIES = libgstpnm.la

libgstpnm_la_SOURCES = gstpnmutils.c gstpnm.c gstpnmdec.c gstpnmenc.c
libgstpnm_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstpnm_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) -lgstvideo-@GST_API_VERSION@
libgstpnm_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpnm_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstpnmdec.h gstpnmutils.h gstpnmenc.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstpnm -:SHARED libgstpnm \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstpnm_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstpnm_la_CFLAGS) \
	 -:LDFLAGS $(libgstpnm_la_LDFLAGS) \
	           $(libgstpnm_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
