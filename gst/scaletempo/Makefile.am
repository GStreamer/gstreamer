plugin_LTLIBRARIES = libgstscaletempoplugin.la

# sources used to compile this plug-in
libgstscaletempoplugin_la_SOURCES = gstscaletempoplugin.c gstscaletempo.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstscaletempoplugin_la_CFLAGS = $(GST_CFLAGS)
libgstscaletempoplugin_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstscaletempoplugin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstscaletempoplugin_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstscaletempo.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstscaletempo -:SHARED libgstscaletempo \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstscaletempo_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstscaletempo_la_CFLAGS) \
	 -:LDFLAGS $(libgstscaletempo_la_LDFLAGS) \
	           $(libgstscaletempo_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
