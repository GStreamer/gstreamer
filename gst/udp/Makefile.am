plugin_LTLIBRARIES = libgstudp.la

libgstudp_la_SOURCES = gstudp.c gstudpsrc.c gstudpsink.c gstmultiudpsink.c gstdynudpsink.c gstudpnetutils.c

libgstudp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_NET_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
libgstudp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_NET_LIBS) $(GIO_LIBS)
libgstudp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstudp_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstudpsink.h gstudpsrc.h gstmultiudpsink.h gstdynudpsink.h gstudpnetutils.h

EXTRA_DIST = README

CLEANFILES = $(BUILT_SOURCES)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstudp -:SHARED libgstudp \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstudp_la_SOURCES) \
	 	   $(nodist_libgstudp_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstudp_la_CFLAGS) \
	 -:LDFLAGS $(libgstudp_la_LDFLAGS) \
	           $(libgstudp_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
