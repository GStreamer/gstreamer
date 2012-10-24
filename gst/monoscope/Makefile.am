plugin_LTLIBRARIES = libgstmonoscope.la

libgstmonoscope_la_SOURCES = gstmonoscope.c monoscope.c convolve.c

noinst_HEADERS = gstmonoscope.h monoscope.h convolve.h

libgstmonoscope_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
libgstmonoscope_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstmonoscope_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmonoscope_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmonoscope -:SHARED libgstmonoscope \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmonoscope_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmonoscope_la_CFLAGS) \
	 -:LDFLAGS $(libgstmonoscope_la_LDFLAGS) \
	           $(libgstmonoscope_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
