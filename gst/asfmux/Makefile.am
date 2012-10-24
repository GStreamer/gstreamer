# plugindir is set in configure

plugin_LTLIBRARIES = libgstasfmux.la

# sources used to compile this plug-in
libgstasfmux_la_SOURCES = gstasfmux.c \
                          gstasfobjects.c \
                          gstasfparse.c \
                          gstrtpasfpay.c \
                          gstasf.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstasfmux_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) 
libgstasfmux_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
                         -lgstrtp-@GST_API_VERSION@
libgstasfmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstasfmux_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstasfmux.h \
                 gstasfobjects.h \
                 gstasfparse.h \
                 gstrtpasfpay.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstasfmux -:SHARED libgstasfmux \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstasfmux_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstasfmux_la_CFLAGS) \
	 -:LDFLAGS $(libgstasfmux_la_LDFLAGS) \
	           $(libgstasfmux_la_LIBADD) \
						 $(libgstasfmux_la_LIBTOOLFLAGS) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
