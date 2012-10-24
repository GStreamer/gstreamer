plugin_LTLIBRARIES = libgstjp2kdecimator.la

libgstjp2kdecimator_la_SOURCES = \
	gstjp2kdecimator.c \
	jp2kcodestream.c

libgstjp2kdecimator_la_CFLAGS = \
    $(GST_CFLAGS)
libgstjp2kdecimator_la_LIBADD = \
    $(GST_LIBS) $(GST_BASE_LIBS)
libgstjp2kdecimator_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstjp2kdecimator_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstjp2kdecimator.h jp2kcodestream.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstjp2kdecimator -:SHARED libgstjp2kdecimator \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstjp2kdecimator_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstjp2kdecimator_la_CFLAGS) \
	 -:LDFLAGS $(libgstjp2kdecimator_la_LDFLAGS) \
	           $(libgstjp2kdecimator_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
