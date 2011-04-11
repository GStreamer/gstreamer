plugin_LTLIBRARIES = libgstlegacyresample.la

resample_SOURCES = \
	functable.c \
	resample.c \
	resample_functable.c \
	resample_ref.c \
	resample.h \
	buffer.c

noinst_HEADERS = \
	gstlegacyresample.h \
	functable.h \
	debug.h \
	buffer.h

libgstlegacyresample_la_SOURCES = gstlegacyresample.c $(resample_SOURCES)
libgstlegacyresample_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstlegacyresample_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstlegacyresample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstlegacyresample_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstlegacyresample -:SHARED libgstlegacyresample \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstlegacyresample_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstlegacyresample_la_CFLAGS) \
	 -:LDFLAGS $(libgstlegacyresample_la_LDFLAGS) \
	           $(libgstlegacyresample_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@