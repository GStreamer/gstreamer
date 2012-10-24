plugin_LTLIBRARIES = libgstivfparse.la

libgstivfparse_la_SOURCES = gstivfparse.c
libgstivfparse_la_CFLAGS = \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstivfparse_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstivfparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstivfparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstivfparse.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstivfparse -:SHARED libgstivfparse \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstivfparse_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstivfparse_la_CFLAGS) \
	 -:LDFLAGS $(libgstivfparse_la_LDFLAGS) \
	           $(libgstivfparse_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@