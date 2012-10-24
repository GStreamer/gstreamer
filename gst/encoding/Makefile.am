plugin_LTLIBRARIES = libgstencodebin.la

libgstencodebin_la_SOURCES = 	\
	gstencodebin.c		\
	gstsmartencoder.c	\
	gststreamcombiner.c	\
	gststreamsplitter.c

libgstencodebin_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstencodebin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstencodebin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
	$(GST_LIBS)
libgstencodebin_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = 		\
	gstencodebin.h		\
	gststreamcombiner.h	\
	gststreamsplitter.h	\
	gstsmartencoder.h

include $(top_srcdir)/common/gst-glib-gen.mak

Android.mk: Makefile.am
	androgenizer \
	-:PROJECT libgstencodebin -:SHARED libgstencodebin \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstencodebin_la_SOURCES) \
	 	   $(nodist_libgstencodebin_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstencodebin_la_CFLAGS) \
	 -:LDFLAGS $(libgstencodebin_la_LDFLAGS) \
	           $(libgstencodebin_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
