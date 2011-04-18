plugin_LTLIBRARIES = libgstrtsp.la

libgstrtsp_la_SOURCES = gstrtsp.c gstrtspsrc.c \
			gstrtpdec.c gstrtspext.c

libgstrtsp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtsp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
		       -lgstinterfaces-@GST_MAJORMINOR@ \
		       -lgstrtp-@GST_MAJORMINOR@ -lgstrtsp-@GST_MAJORMINOR@ \
		       -lgstsdp-@GST_MAJORMINOR@ $(GST_LIBS) $(WIN32_LIBS)
libgstrtsp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtsp_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstrtspsrc.h     \
		 gstrtsp.h        \
		 gstrtpdec.h      \
		 gstrtspext.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstrtsp -:SHARED libgstrtsp \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstrtsp_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstrtsp_la_CFLAGS) \
	 -:LDFLAGS $(libgstrtsp_la_LDFLAGS) \
	           $(libgstrtsp_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
