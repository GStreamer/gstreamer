
plugin_LTLIBRARIES = libgstspeed.la

libgstspeed_la_SOURCES = gstspeed.c
libgstspeed_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstspeed_la_LIBADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) $(LIBM) -lgstaudio-$(GST_API_VERSION)
libgstspeed_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstspeed_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstspeed.h

#if HAVE_GTK
#noinst_PROGRAMS = demo-mp3
#endif

#demo_mp3_SOURCES = demo-mp3.c
#demo_mp3_CFLAGS  = $(GTK_CFLAGS) $(GST_CFLAGS) 
#demo_mp3_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstspeed -:SHARED libgstspeed \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstspeed_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstspeed_la_CFLAGS) \
	 -:LDFLAGS $(libgstspeed_la_LDFLAGS) \
	           $(libgstspeed_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
