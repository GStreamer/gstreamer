plugin_LTLIBRARIES = libgstcolorspace.la

ORC_SOURCE=gstcolorspaceorc
include $(top_srcdir)/common/orc.mak

libgstcolorspace_la_SOURCES = gstcolorspace.c colorspace.c
nodist_libgstcolorspace_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstcolorspace_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(ORC_CFLAGS)
libgstcolorspace_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ORC_LIBS)
libgstcolorspace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcolorspace_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstcolorspace.h colorspace.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstcolorspace -:SHARED libgstcolorspace \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstcolorspace_la_SOURCES) \
	           $(nodist_libgstcolorspace_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstcolorspace_la_CFLAGS) \
	 -:LDFLAGS $(libgstcolorspace_la_LDFLAGS) \
	           $(libgstcolorspace_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
