plugin_LTLIBRARIES	= libgstfreeze.la

libgstfreeze_la_SOURCES	= gstfreeze.c
libgstfreeze_la_CFLAGS	= $(GST_CFLAGS)
libgstfreeze_la_LIBADD	= ${GST_LIBS}
libgstfreeze_la_LDFLAGS	= $(GST_PLUGIN_LDFLAGS)
libgstfreeze_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS		= gstfreeze.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstfreeze -:SHARED libgstfreeze \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstfreeze_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstfreeze_la_CFLAGS) \
	 -:LDFLAGS $(libgstfreeze_la_LDFLAGS) \
	           $(libgstfreeze_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@