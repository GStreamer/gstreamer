plugin_LTLIBRARIES = libgstreplaygain.la

libgstreplaygain_la_SOURCES = \
	gstrganalysis.c       \
	gstrglimiter.c        \
	gstrgvolume.c         \
	replaygain.c          \
	rganalysis.c
libgstreplaygain_la_CFLAGS  = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstreplaygain_la_LIBADD  = \
	$(GST_PLUGINS_BASE_LIBS) -lgstpbutils-$(GST_API_VERSION) -lgstaudio-$(GST_API_VERSION)\
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstreplaygain_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstreplaygain_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS =         \
	gstrganalysis.h  \
	gstrglimiter.h   \
	gstrgvolume.h    \
	replaygain.h     \
	rganalysis.h


Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstreplaygain -:SHARED libgstreplaygain \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstreplaygain_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstreplaygain_la_CFLAGS) \
	 -:LDFLAGS $(libgstreplaygain_la_LDFLAGS) \
	           $(libgstreplaygain_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
