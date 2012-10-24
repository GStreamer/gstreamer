plugin_LTLIBRARIES = libgstmultipart.la

libgstmultipart_la_SOURCES = multipart.c multipartdemux.c multipartmux.c
libgstmultipart_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstmultipart_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstmultipart_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmultipart_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = multipartdemux.h multipartmux.h


Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmultipart -:SHARED libgstmultipart \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmultipart_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmultipart_la_CFLAGS) \
	 -:LDFLAGS $(libgstmultipart_la_LDFLAGS) \
	           $(libgstmultipart_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
