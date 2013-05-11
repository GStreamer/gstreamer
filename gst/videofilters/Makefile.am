plugin_LTLIBRARIES = libgstvideofiltersbad.la

#ORC_SOURCE=gstvideofiltersbadorc
#include $(top_srcdir)/common/orc.mak

libgstvideofiltersbad_la_SOURCES = \
	gstzebrastripe.c \
	gstzebrastripe.h \
	gstscenechange.c \
	gstvideodiff.c \
	gstvideodiff.h \
	gstvideofiltersbad.c
#nodist_libgstvideofiltersbad_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstvideofiltersbad_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(ORC_CFLAGS)
libgstvideofiltersbad_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ORC_LIBS) \
	$(LIBM)
libgstvideofiltersbad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideofiltersbad_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstzebrastripe.h \
	gstscenechange.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstvideofilters -:SHARED libgstvideofilters \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvideofilters_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvideofilters_la_CFLAGS) \
	 -:LDFLAGS $(libgstvideofilters_la_LDFLAGS) \
	           $(libgstvideofilters_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
