plugin_LTLIBRARIES = libgstfreeverb.la

# sources used to compile this plug-in
libgstfreeverb_la_SOURCES = gstfreeverb.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstfreeverb_la_CFLAGS = $(GST_CFLAGS) \
  $(GST_BASE_CFLAGS) \
  $(GST_PLUGINS_BASE_CFLAGS)
libgstfreeverb_la_LIBADD = $(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) \
	$(LIBM)
libgstfreeverb_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfreeverb_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstfreeverb.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstfreeverb -:SHARED libgstfreeverb \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstfreeverb_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstfreeverb_la_CFLAGS) \
	 -:LDFLAGS $(libgstfreeverb_la_LDFLAGS) \
	           $(libgstfreeverb_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
