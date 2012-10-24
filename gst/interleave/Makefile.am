
plugin_LTLIBRARIES = libgstinterleave.la

libgstinterleave_la_SOURCES = plugin.c interleave.c deinterleave.c
libgstinterleave_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstinterleave_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(GST_BASE_LIBS) $(GST_LIBS)
libgstinterleave_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstinterleave_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = plugin.h interleave.h deinterleave.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstinterleave -:SHARED libgstinterleave \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstinterleave_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstinterleave_la_CFLAGS) \
	 -:LDFLAGS $(libgstinterleave_la_LDFLAGS) \
	           $(libgstinterleave_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
