plugin_LTLIBRARIES = libgstvideosignal.la 

noinst_HEADERS = gstvideoanalyse.h gstvideodetect.h gstvideomark.h

libgstvideosignal_la_SOURCES = gstvideosignal.c   \
                               gstvideoanalyse.c  \
                               gstvideodetect.c   \
                               gstvideomark.c

libgstvideosignal_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
			    $(GST_PLUGINS_BASE_CFLAGS)
libgstvideosignal_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ $(GST_BASE_LIBS) $(GST_LIBS)
libgstvideosignal_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideosignal_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstvideosignal -:SHARED libgstvideosignal \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvideosignal_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvideosignal_la_CFLAGS) \
	 -:LDFLAGS $(libgstvideosignal_la_LDFLAGS) \
	           $(libgstvideosignal_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@