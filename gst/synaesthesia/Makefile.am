plugin_LTLIBRARIES = libgstsynaesthesia.la

libgstsynaesthesia_la_SOURCES = gstsynaesthesia.c synaescope.c

noinst_HEADERS = synaescope.h gstsynaesthesia.h

libgstsynaesthesia_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstsynaesthesia_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstsynaesthesia_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsynaesthesia_la_LIBTOOLFLAGS = --tag=disable-static

EXTRA_DIST = README

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstsynaesthesia -:SHARED libgstsynaesthesia \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstsynaesthesia_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstsynaesthesia_la_CFLAGS) \
	 -:LDFLAGS $(libgstsynaesthesia_la_LDFLAGS) \
	           $(libgstsynaesthesia_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
