plugin_LTLIBRARIES = libgsth264parse.la

libgsth264parse_la_SOURCES = \
	gsth264parse.c

noinst_HEADERS = \
	gsth264parse.h

libgsth264parse_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgsth264parse_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgsth264parse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsth264parse_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgsth264parse -:SHARED libgsth264parse \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgsth264parse_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgsth264parse_la_CFLAGS) \
	 -:LDFLAGS $(libgsth264parse_la_LDFLAGS) \
	           $(libgsth264parse_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
