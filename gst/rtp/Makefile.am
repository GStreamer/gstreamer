plugin_LTLIBRARIES = libgstrtpbad.la

libgstrtpbad_la_SOURCES = \
	gstrtp.c \
	gstrtph265depay.c \
	gstrtph265pay.c

libgstrtpbad_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstrtpbad_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-$(GST_API_VERSION).la \
	-lgstaudio-@GST_API_VERSION@ \
	-lgstvideo-@GST_API_VERSION@ \
	-lgsttag-@GST_API_VERSION@ \
	-lgstrtp-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) \
	$(LIBM)
libgstrtpbad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtpbad_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS =			\
	gstrtph265depay.h \
	gstrtph265pay.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstrtpbad -:SHARED libgstrtpbad \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstrtpbad_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstrtpbad_la_CFLAGS) \
	 -:LDFLAGS $(libgstrtpbad_la_LDFLAGS) \
	  $(libgstrtpbad_la_LIBADD) \
	  -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	      LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
