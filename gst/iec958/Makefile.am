plugin_LTLIBRARIES = libgstiec958.la

libgstiec958_la_SOURCES = ac3iec.c ac3_padder.c
libgstiec958_la_CFLAGS = $(GST_CFLAGS)
libgstiec958_la_LIBADD = $(GST_LIBS)
libgstiec958_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstiec958_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = ac3_padder.h ac3iec.h 

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstiec958 -:SHARED libgstiec958 \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstiec958_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstiec958_la_CFLAGS) \
	 -:LDFLAGS $(libgstiec958_la_LDFLAGS) \
	           $(libgstiec958_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
