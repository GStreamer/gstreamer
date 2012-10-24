plugin_LTLIBRARIES = libgsttypefindfunctions.la

libgsttypefindfunctions_la_SOURCES = gsttypefindfunctions.c
libgsttypefindfunctions_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) $(GIO_CFLAGS)
libgsttypefindfunctions_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttypefindfunctions_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(GIO_LIBS)

libgsttypefindfunctions_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgsttypefindfunctions -:SHARED libgsttypefindfunctions \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgsttypefindfunctions_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgsttypefindfunctions_la_CFLAGS) \
	 -:LDFLAGS $(libgsttypefindfunctions_la_LDFLAGS) \
	           $(libgsttypefindfunctions_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
