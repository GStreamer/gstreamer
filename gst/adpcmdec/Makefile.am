plugin_LTLIBRARIES = libgstadpcmdec.la

# sources used to compile this plug-in
libgstadpcmdec_la_SOURCES = adpcmdec.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstadpcmdec_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstadpcmdec_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstadpcmdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstadpcmdec_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstadpcmdec -:SHARED libgstadpcmdec \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstadpcmdec_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstadpcmdec_la_CFLAGS) \
	 -:LDFLAGS $(libgstadpcmdec_la_LDFLAGS) \
	           $(libgstadpcmdec_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
