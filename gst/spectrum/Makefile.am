plugin_LTLIBRARIES = libgstspectrum.la

libgstspectrum_la_SOURCES = gstspectrum.c
libgstspectrum_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstspectrum_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstfft-$(GST_API_VERSION) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstspectrum_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstspectrum_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstspectrum.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstspectrum -:SHARED libgstspectrum \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstspectrum_la_SOURCES) \
	 	   $(nodist_libgstspectrum_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstspectrum_la_CFLAGS) \
	 -:LDFLAGS $(libgstspectrum_la_LDFLAGS) \
	           $(libgstspectrum_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
