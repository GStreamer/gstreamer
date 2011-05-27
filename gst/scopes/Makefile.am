plugin_LTLIBRARIES = libgstscopes.la

libgstscopes_la_SOURCES = \
    gstbasescope.c plugin.c \
    gstspectrascope.c gstspectrascope.h \
    gstwavescope.c gstwavescope.h

libgstscopes_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS)\
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstscopes_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) \
	-lgstvideo-$(GST_MAJORMINOR) -lgstfft-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstscopes_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstscopes_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstbasescope.h \
	gstspectrascope.h gstwavescope.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT scopes -:SHARED scopes \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstscopes_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstscopes_la_CFLAGS) \
	 -:LDFLAGS $(libgstscopes_la_LDFLAGS) \
	           $(libgstscopes_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@

