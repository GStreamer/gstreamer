plugin_LTLIBRARIES = libgstvideoscale.la

ORC_SOURCE=gstvideoscaleorc
include $(top_srcdir)/common/orc.mak

libgstvideoscale_la_SOURCES = \
	gstvideoscale.c \
	vs_image.c \
	vs_scanline.c \
	vs_4tap.c \
	vs_fill_borders.c \
	vs_lanczos.c

nodist_libgstvideoscale_la_SOURCES = $(ORC_NODIST_SOURCES)

libgstvideoscale_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstvideoscale_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideoscale_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS) $(LIBM)
libgstvideoscale_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstvideoscale.h \
	vs_image.h \
	vs_scanline.h \
	vs_4tap.h \
	vs_fill_borders.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstvideoscale -:SHARED libgstvideoscale \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvideoscale_la_SOURCES) \
	 	   $(nodist_libgstvideoscale_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvideoscale_la_CFLAGS) \
	 -:LDFLAGS $(libgstvideoscale_la_LDFLAGS) \
	           $(libgstvideoscale_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
