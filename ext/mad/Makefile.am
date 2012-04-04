plugin_LTLIBRARIES = libgstmad.la 

libgstmad_la_SOURCES = gstmad.c

libgstmad_la_CFLAGS = \
        $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	$(MAD_CFLAGS)
libgstmad_la_LIBADD = \
        $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(MAD_LIBS)
libgstmad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmad_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstmad.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmad -:SHARED libgstmad \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmad_la_SOURCES) \
	 -:CPPFLAGS $(CPPFLAGS) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmad_la_CFLAGS) \
	 -:LDFLAGS $(libgstmad_la_LDFLAGS) \
	           $(libgstmad_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
