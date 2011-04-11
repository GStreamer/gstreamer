# plugindir is set in configure

plugin_LTLIBRARIES = libgstqtmux.la

# sources used to compile this plug-in
libgstqtmux_la_SOURCES = gstqtmux.c \
                        gstqtmoovrecover.c \
                        gstqtmuxplugin.c \
			atoms.c \
                        atomsrecovery.c \
			descriptors.c \
			properties.c \
			gstqtmuxmap.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstqtmux_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstqtmux_la_LIBADD = -lgstinterfaces-$(GST_MAJORMINOR) $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR)
libgstqtmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstqtmux_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstqtmux.h \
                gstqtmoovrecover.h \
		atoms.h \
                atomsrecovery.h \
		descriptors.h \
		properties.h \
		fourcc.h \
		ftypcc.h \
		gstqtmuxmap.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstqtmux -:SHARED libgstqtmux \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstqtmux_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstqtmux_la_CFLAGS) \
	 -:LDFLAGS $(libgstqtmux_la_LDFLAGS) \
	           $(libgstqtmux_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
