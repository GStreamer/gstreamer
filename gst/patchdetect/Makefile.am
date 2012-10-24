plugin_LTLIBRARIES = libgstpatchdetect.la

#ORC_SOURCE=gstpatchdetectorc
#include $(top_srcdir)/common/orc.mak

libgstpatchdetect_la_SOURCES = gstpatchdetect.c
#nodist_libgstpatchdetect_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstpatchdetect_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstpatchdetect_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM)
libgstpatchdetect_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpatchdetect_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstpatchdetect.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstpatchdetect -:SHARED libgstpatchdetect \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstpatchdetect_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstpatchdetect_la_CFLAGS) \
	 -:LDFLAGS $(libgstpatchdetect_la_LDFLAGS) \
	           $(libgstpatchdetect_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
