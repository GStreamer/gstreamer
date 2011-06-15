plugin_LTLIBRARIES = libgstvideomaxrate.la

libgstvideomaxrate_la_SOURCES = videomaxrate.c

noinst_HEADERS = videomaxrate.h
libgstvideomaxrate_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstvideomaxrate_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstvideomaxrate_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideomaxrate_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstvideomaxrate -:SHARED libgstvideomaxrate \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvideomaxrate_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvideomaxrate_la_CFLAGS) \
	 -:LDFLAGS $(libgstvideomaxrate_la_LDFLAGS) \
	           $(libgstvideomaxrate_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@