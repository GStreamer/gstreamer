
plugin_LTLIBRARIES = libgstfestival.la

libgstfestival_la_SOURCES = gstfestival.c 
libgstfestival_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
# we use net connection now... perhaps use the lib later
# libgstfestival_la_LIBADD = $(FESTIVAL_LIBS)
libgstfestival_la_LIBADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) \
                           -lgstaudio-1.0 $(WINSOCK2_LIBS)
libgstfestival_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfestival_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstfestival.h 

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstfestival -:SHARED libgstfestival \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstfestival_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstfestival_la_CFLAGS) \
	 -:LDFLAGS $(libgstfestival_la_LDFLAGS) \
	           $(libgstfestival_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-1.0' \
	> $@
