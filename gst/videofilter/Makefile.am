plugin_LTLIBRARIES = libgstvideofilter.la

noinst_HEADERS = gstvideoflip.h gstvideobalance.h gstgamma.h gstvideomedian.h

EXTRA_DIST = gstvideotemplate.c make_filter
CLEANFILES = gstvideoexample.c

libgstvideofilter_la_SOURCES = plugin.c \
			gstvideoflip.c \
			gstvideobalance.c \
			gstgamma.c \
			gstvideomedian.c
libgstvideofilter_la_CFLAGS = $(GST_CFLAGS) \
			$(GST_BASE_CFLAGS) \
			$(GST_PLUGINS_BASE_CFLAGS)
libgstvideofilter_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
			-lgstvideo-@GST_API_VERSION@ \
			$(GST_BASE_LIBS) $(GST_LIBS)
libgstvideofilter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)
libgstvideofilter_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

gstvideoexample.c: $(srcdir)/make_filter $(srcdir)/gstvideotemplate.c
	$(srcdir)/make_filter Videoexample $(srcdir)/gstvideotemplate.c

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstvideofilter -:SHARED libgstvideofilter \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvideofilter_la_SOURCES) \
	 	   $(nodist_libgstvideofilter_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvideofilter_la_CFLAGS) \
	 -:LDFLAGS $(libgstvideofilter_la_LDFLAGS) \
	           $(libgstvideofilter_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
