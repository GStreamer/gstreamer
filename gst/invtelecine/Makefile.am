plugin_LTLIBRARIES = libgstinvtelecine.la

libgstinvtelecine_la_SOURCES = \
	gstinvtelecine.c

libgstinvtelecine_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)

libgstinvtelecine_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ \
	$(LIBM)

libgstinvtelecine_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstinvtelecine_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstinvtelecine -:SHARED libgstinvtelecine \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstinvtelecine_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstinvtelecine_la_CFLAGS) \
	 -:LDFLAGS $(libgstinvtelecine_la_LDFLAGS) \
	           $(libgstinvtelecine_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@