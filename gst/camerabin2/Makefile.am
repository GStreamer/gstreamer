plugin_LTLIBRARIES = libgstcamerabin2.la

libgstcamerabin2_la_SOURCES = gstviewfinderbin.c \
				camerabingeneral.c \
				gstwrappercamerabinsrc.c \
				gstcamerabin2.c \
				gstplugin.c

libgstcamerabin2_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	-DGST_USE_UNSTABLE_API

libgstcamerabin2_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstphotography-$(GST_MAJORMINOR).la \
	$(top_builddir)/gst-libs/gst/basecamerabinsrc/libgstbasecamerabinsrc-$(GST_MAJORMINOR).la \
	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR) -lgsttag-$(GST_MAJORMINOR) -lgstapp-$(GST_MAJORMINOR) -lgstpbutils-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS)

libgstcamerabin2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcamerabin2_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstviewfinderbin.h \
		camerabingeneral.h \
		gstwrappercamerabinsrc.h \
		gstcamerabin2.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstcamerabin2 -:SHARED libgstcamerabin2 \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstcamerabin2_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstcamerabin2_la_CFLAGS) \
	 -:LDFLAGS $(libgstcamerabin2_la_LDFLAGS) \
	           $(libgstcamerabin2_la_LIBADD) \
	           -ldl \
	 -:LIBFILTER_STATIC gstphotography-@GST_MAJORMINOR@ \
	          gstbasecamerabinsrc-@GST_MAJORMINOR@ \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
