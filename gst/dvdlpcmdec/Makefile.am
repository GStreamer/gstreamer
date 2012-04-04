
plugin_LTLIBRARIES = libgstdvdlpcmdec.la

libgstdvdlpcmdec_la_SOURCES = gstdvdlpcmdec.c
libgstdvdlpcmdec_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstdvdlpcmdec_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ $(GST_LIBS)
libgstdvdlpcmdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdvdlpcmdec_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstdvdlpcmdec.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstdvdlpcmdec -:SHARED libgstdvdlpcmdec \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstdvdlpcmdec_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstdvdlpcmdec_la_CFLAGS) \
	 -:LDFLAGS $(libgstdvdlpcmdec_la_LDFLAGS) \
	           $(libgstdvdlpcmdec_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
