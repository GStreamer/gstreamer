glib_gen_prefix = __gst_camerabin
glib_gen_basename = gstcamerabin

include $(top_srcdir)/common/gst-glib-gen.mak

built_sources = gstcamerabin-marshal.c
built_headers = gstcamerabin-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gstcamerabin-marshal.list

plugin_LTLIBRARIES = libgstcamerabin.la

libgstcamerabin_la_SOURCES = gstcamerabin.c             \
			  gstcamerabincolorbalance.c \
			  gstinputselector.c         \
			  camerabinimage.c           \
			  camerabinvideo.c           \
			  camerabingeneral.c         \
			  camerabinpreview.c         \
			  gstcamerabin-enum.c

nodist_libgstcamerabin_la_SOURCES = $(built_sources)
libgstcamerabin_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API
libgstcamerabin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstphotography-$(GST_MAJORMINOR).la \
	$(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstinterfaces-$(GST_MAJORMINOR) -lgsttag-$(GST_MAJORMINOR)

libgstcamerabin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcamerabin_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstcamerabin.h             \
		 gstcamerabincolorbalance.h \
		 gstinputselector.h         \
		 camerabinimage.h           \
		 camerabinvideo.h           \
		 camerabindebug.h           \
		 camerabingeneral.h         \
		 camerabinpreview.h         \
		 gstcamerabin-enum.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstcamerabin -:SHARED libgstcamerabin \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstcamerabin_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstcamerabin_la_CFLAGS) \
	 -:LDFLAGS $(libgstcamerabin_la_LDFLAGS) \
	           $(libgstcamerabin_la_LIBADD) \
	           -ldl \
	 -:LIBFILTER_STATIC gstphotography-@GST_MAJORMINOR@ \
	          gstbasecamerabinsrc-@GST_MAJORMINOR@ \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
