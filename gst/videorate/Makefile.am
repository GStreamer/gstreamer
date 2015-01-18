noinst_HEADERS = gstvideorate.h

plugin_LTLIBRARIES = libgstvideorate.la

libgstvideorate_la_SOURCES = gstvideorate.c
libgstvideorate_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstvideorate_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideorate_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstvideorate_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstvideorate -:SHARED libgstvideorate \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvideorate_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvideorate_la_CFLAGS) \
	 -:LDFLAGS $(libgstvideorate_la_LDFLAGS) \
	           $(libgstvideorate_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
