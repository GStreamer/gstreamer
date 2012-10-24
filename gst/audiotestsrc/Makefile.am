plugin_LTLIBRARIES = libgstaudiotestsrc.la

libgstaudiotestsrc_la_SOURCES = gstaudiotestsrc.c
libgstaudiotestsrc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstaudiotestsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiotestsrc_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstaudiotestsrc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstaudiotestsrc.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstaudiotestsrc -:SHARED libgstaudiotestsrc \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstaudiotestsrc_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstaudiotestsrc_la_CFLAGS) \
	 -:LDFLAGS $(libgstaudiotestsrc_la_LDFLAGS) \
	           $(libgstaudiotestsrc_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
