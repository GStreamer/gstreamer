
plugin_LTLIBRARIES = libgstrawparse.la

libgstrawparse_la_SOURCES = \
	gstrawparse.c \
	gstaudioparse.c \
	gstvideoparse.c \
	plugin.c
libgstrawparse_la_CFLAGS = \
		$(GST_PLUGINS_BASE_CFLAGS) \
		$(GST_BASE_CFLAGS) \
		$(GST_CFLAGS)
libgstrawparse_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_API_VERSION@ -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstrawparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrawparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstaudioparse.h \
	gstrawparse.h \
	gstvideoparse.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstrawparse -:SHARED libgstrawparse \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstrawparse_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstrawparse_la_CFLAGS) \
	 -:LDFLAGS $(libgstrawparse_la_LDFLAGS) \
	           $(libgstrawparse_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
