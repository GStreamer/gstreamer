plugin_LTLIBRARIES = libgstalaw.la libgstmulaw.la

libgstalaw_la_SOURCES = alaw-encode.c alaw-decode.c alaw.c
libgstalaw_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstalaw_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstalaw_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstalaw_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

libgstmulaw_la_SOURCES = mulaw-encode.c mulaw-conversion.c mulaw-decode.c mulaw.c
libgstmulaw_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstmulaw_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstmulaw_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmulaw_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = mulaw-conversion.h alaw-encode.h alaw-decode.h mulaw-encode.h mulaw-decode.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstalaw -:SHARED libgstalaw \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstalaw_la_SOURCES) \
	 	   $(nodist_libgstalaw_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstalaw_la_CFLAGS) \
	 -:LDFLAGS $(libgstalaw_la_LDFLAGS) \
	           $(libgstalaw_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	-:SHARED libgstmulaw \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmulaw_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmulaw_la_CFLAGS) \
	 -:LDFLAGS $(libgstmulaw_la_LDFLAGS) \
	           $(libgstmulaw_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
