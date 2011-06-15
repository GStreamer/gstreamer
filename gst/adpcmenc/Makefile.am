plugin_LTLIBRARIES = libgstadpcmenc.la

# sources used to compile this plug-in
libgstadpcmenc_la_SOURCES = adpcmenc.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstadpcmenc_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstadpcmenc_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstadpcmenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstadpcmenc_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstadpcmenc -:SHARED libgstadpcmenc \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstadpcmenc_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstadpcmenc_la_CFLAGS) \
	 -:LDFLAGS $(libgstadpcmenc_la_LDFLAGS) \
	           $(libgstadpcmenc_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
