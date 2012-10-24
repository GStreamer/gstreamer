
plugin_LTLIBRARIES = libgstdvdspu.la

libgstdvdspu_la_SOURCES = gstdvdspu.c gstdvdspu-render.c gstspu-vobsub.c gstspu-vobsub-render.c gstspu-pgs.c

libgstdvdspu_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstdvdspu_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
     $(GST_LIBS)
libgstdvdspu_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdvdspu_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstdvdspu.h gstspu-pgs.h gstspu-vobsub.h gstspu-common.h

EXTRA_DIST = Notes.txt

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstdvdspu -:SHARED libgstdvdspu \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstdvdspu_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstdvdspu_la_CFLAGS) \
	 -:LDFLAGS $(libgstdvdspu_la_LDFLAGS) \
	           $(libgstdvdspu_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
