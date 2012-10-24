plugin_LTLIBRARIES = libgstcdxaparse.la

libgstcdxaparse_la_SOURCES = \
	gstcdxaparse.c \
	gstvcdparse.c


noinst_HEADERS = \
	gstcdxaparse.h \
	gstvcdparse.h

libgstcdxaparse_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)

libgstcdxaparse_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstriff-@GST_API_VERSION@

libgstcdxaparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcdxaparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstcdxaparse -:SHARED libgstcdxaparse \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstcdxaparse_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstcdxaparse_la_CFLAGS) \
	 -:LDFLAGS $(libgstcdxaparse_la_LDFLAGS) \
	           $(libgstcdxaparse_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@