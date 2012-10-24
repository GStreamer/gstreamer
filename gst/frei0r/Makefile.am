plugin_LTLIBRARIES = libgstfrei0r.la

libgstfrei0r_la_SOURCES = \
	gstfrei0r.c \
	gstfrei0rfilter.c \
	gstfrei0rsrc.c \
	gstfrei0rmixer.c

libgstfrei0r_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstfrei0r_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(GMODULE_NO_EXPORT_LIBS)
libgstfrei0r_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfrei0r_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstfrei0r.h gstfrei0rfilter.h gstfrei0rsrc.h gstfrei0rmixer.h frei0r.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstfrei0r -:SHARED libgstfrei0r \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstfrei0r_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstfrei0r_la_CFLAGS) \
	 -:LDFLAGS $(libgstfrei0r_la_LDFLAGS) \
	           $(libgstfrei0r_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
