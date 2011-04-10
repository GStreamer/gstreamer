
plugin_LTLIBRARIES = libgstqtdemux.la

libgstqtdemux_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstqtdemux_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) \
    -lgstriff-@GST_MAJORMINOR@ \
    -lgstaudio-@GST_MAJORMINOR@ \
    -lgstrtp-@GST_MAJORMINOR@ \
    -lgsttag-@GST_MAJORMINOR@ \
    -lgstpbutils-@GST_MAJORMINOR@ \
    $(GST_BASE_LIBS) $(GST_LIBS) $(ZLIB_LIBS)
libgstqtdemux_la_LDFLAGS = ${GST_PLUGIN_LDFLAGS} 
libgstqtdemux_la_SOURCES = quicktime.c gstrtpxqtdepay.c \
	qtdemux.c qtdemux_types.c qtdemux_dump.c qtdemux_lang.c
libgstqtdemux_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	qtatomparser.h   \
	qtdemux.h        \
	qtdemux_types.h  \
	qtdemux_dump.h   \
	qtdemux_fourcc.h \
	qtdemux_lang.h   \
	qtpalette.h      \
	gstrtpxqtdepay.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstqtdemux -:SHARED libgstqtdemux \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstqtdemux_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(CPPFLAGS) $(libgstqtdemux_la_CFLAGS) \
	 -:LDFLAGS $(libgstqtdemux_la_LDFLAGS) \
	           $(libgstqtdemux_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
