plugin_LTLIBRARIES = libgstdataurisrc.la

libgstdataurisrc_la_SOURCES = gstdataurisrc.c gstdataurisrc.h

libgstdataurisrc_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstdataurisrc_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstdataurisrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdataurisrc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstdataurisrc -:SHARED libgstdataurisrc \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstdataurisrc_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstdataurisrc_la_CFLAGS) \
	 -:LDFLAGS $(libgstdataurisrc_la_LDFLAGS) \
	           $(libgstdataurisrc_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@