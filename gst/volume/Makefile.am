plugin_LTLIBRARIES = libgstvolume.la

ORC_SOURCE=gstvolumeorc
include $(top_srcdir)/common/orc.mak

libgstvolume_la_SOURCES = gstvolume.c
nodist_libgstvolume_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstvolume_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstvolume_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvolume_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-$(GST_API_VERSION).la  \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ORC_LIBS)
libgstvolume_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstvolume.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstvolume -:SHARED libgstvolume \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvolume_la_SOURCES) \
	  $(nodist_libgstvolume_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvolume_la_CFLAGS) \
	 -:LDFLAGS $(libgstvolume_la_LDFLAGS) \
	           $(libgstvolume_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
