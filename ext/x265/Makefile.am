plugin_LTLIBRARIES = libgstx265.la

libgstx265_la_SOURCES = gstx265enc.c
libgstx265_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(X265_CFLAGS)
libgstx265_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GST_LIBS) \
	$(X265_LIBS)
libgstx265_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstx265_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstx265enc.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstx265 -:SHARED libgstx265 \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstx265_la_SOURCES) \
	 -:CPPFLAGS $(CPPFLAGS) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstx265_la_CFLAGS) -I'$$(GSTREAMER_AGGREGATE_TOP)/x265' \
	 -:LDFLAGS $(libgstx265_la_LDFLAGS) \
	           $(libgstx265_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
