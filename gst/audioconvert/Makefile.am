plugin_LTLIBRARIES = libgstaudioconvert.la

ORC_SOURCE=gstaudioconvertorc
include $(top_srcdir)/common/orc.mak

libgstaudioconvert_la_SOURCES = \
	gstaudioconvert.c \
	audioconvert.c \
	gstchannelmix.c \
	gstaudioquantize.c \
	plugin.c
nodist_libgstaudioconvert_la_SOURCES = $(ORC_NODIST_SOURCES)

libgstaudioconvert_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstaudioconvert_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudioconvert_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS) $(LIBM)
libgstaudioconvert_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstaudioconvert.h \
	audioconvert.h \
	gstchannelmix.h \
	gstaudioquantize.h \
	gstfastrandom.h \
	plugin.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstaudioconvert -:SHARED libgstaudioconvert \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstaudioconvert_la_SOURCES) \
	 	   $(nodist_libgstaudioconvert_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstaudioconvert_la_CFLAGS) \
	 -:LDFLAGS $(libgstaudioconvert_la_LDFLAGS) \
	           $(libgstaudioconvert_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
