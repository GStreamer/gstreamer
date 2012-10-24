plugin_LTLIBRARIES = libgsthdvparse.la

libgsthdvparse_la_SOURCES = \
	gsthdvparse.c

noinst_HEADERS = \
	gsthdvparse.h

libgsthdvparse_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgsthdvparse_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(LIBM)
libgsthdvparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsthdvparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgsthdvparse -:SHARED libgsthdvparse \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgsthdvparse_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgsthdvparse_la_CFLAGS) \
	 -:LDFLAGS $(libgsthdvparse_la_LDFLAGS) \
	           $(libgsthdvparse_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@