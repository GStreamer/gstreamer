plugin_LTLIBRARIES = libgstaudiotestsrc.la

libgstaudiotestsrc_la_SOURCES = gstaudiotestsrc.c
libgstaudiotestsrc_la_CFLAGS = $(GST_CFLAGS) $(GST_CONTROLLER_CFLAGS)
libgstaudiotestsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiotestsrc_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(GST_CONTROLLER_LIBS) $(LIBM)
libgstaudiotestsrc_la_LIBTOOLFLAGS = --tag=disable-static

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
