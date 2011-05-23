plugin_LTLIBRARIES = libgstmatroska.la

libgstmatroska_la_SOURCES = \
	ebml-read.c \
	ebml-write.c \
	matroska.c \
	matroska-demux.c \
	matroska-parse.c \
	matroska-ids.c \
	matroska-mux.c \
	matroska-read-common.c \
	webm-mux.c \
	lzo.c

noinst_HEADERS = \
	ebml-ids.h \
	ebml-read.h \
	ebml-write.h \
	matroska-demux.h \
	matroska-parse.h \
	matroska-ids.h \
	matroska-mux.h \
	matroska-read-common.h \
	webm-mux.h \
	lzo.h

libgstmatroska_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstmatroska_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstriff-@GST_MAJORMINOR@ \
	-lgstaudio-@GST_MAJORMINOR@ \
	-lgsttag-@GST_MAJORMINOR@ \
	-lgstpbutils-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ZLIB_LIBS) \
	$(BZ2_LIBS) \
	$(LIBM)
libgstmatroska_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmatroska_la_LIBTOOLFLAGS = --tag=disable-static


Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmatroska -:SHARED libgstmatroska \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmatroska_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(CPPFLAGS) $(libgstmatroska_la_CFLAGS) \
	 -:LDFLAGS $(libgstmatroska_la_LDFLAGS) \
	           $(libgstmatroska_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
