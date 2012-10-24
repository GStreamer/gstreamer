plugin_LTLIBRARIES = libgstsubenc.la

libgstsubenc_la_SOURCES = \
	gstsrtenc.c \
	gstsubenc.c \
	gstwebvttenc.c
libgstsubenc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstsubenc_la_LIBADD = $(GST_BASE_LIBS)
libgstsubenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsubenc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstsrtenc.h \
	gstwebvttenc.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstsubenc -:SHARED libgstsubenc \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstsubenc_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstsubenc_la_CFLAGS) \
	 -:LDFLAGS $(libgstsubenc_la_LDFLAGS) \
	           $(libgstsubenc_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
