plugin_LTLIBRARIES = libgstgoom2k1.la

GOOM_FILTER_FILES = filters.c
GOOM_FILTER_CFLAGS = -UMMX -UUSE_ASM

noinst_HEADERS = gstgoom.h filters.h goom_core.h goom_tools.h graphic.h lines.h 

libgstgoom2k1_la_SOURCES = gstgoom.c goom_core.c $(GOOM_FILTER_FILES) graphic.c lines.c

libgstgoom2k1_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GOOM_FILTER_CFLAGS)
libgstgoom2k1_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstgoom2k1_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgoom2k1_la_LIBTOOLFLAGS = --tag=disable-static

EXTRA_DIST = filters.c

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstgoom2k1 -:SHARED libgstgoom2k1 \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstgoom2k1_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstgoom2k1_la_CFLAGS) \
	 -:LDFLAGS $(libgstgoom2k1_la_LDFLAGS) \
	           $(libgstgoom2k1_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
