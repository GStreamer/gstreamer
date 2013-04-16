lib_LTLIBRARIES = libgsturidownloader-@GST_API_VERSION@.la

libgsturidownloader_@GST_API_VERSION@_la_SOURCES = \
	gstfragment.c gsturidownloader.c

libgsturidownloader_@GST_API_VERSION@includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/uridownloader

libgsturidownloader_@GST_API_VERSION@include_HEADERS = \
	gstfragment.h gsturidownloader.h gsturidownloader_debug.h

libgsturidownloader_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS)

libgsturidownloader_@GST_API_VERSION@_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

libgsturidownloader_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)

Android.mk:  $(BUILT_SOURCES) Makefile.am
	androgenizer -:PROJECT libgsturidownloader -:STATIC libgsturidownloader-@GST_API_VERSION@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgsturidownloader_@GST_API_VERSION@_la_SOURCES) \
         $(built_sources) \
	 -:CFLAGS $(DEFS) $(libgsturidownloader_@GST_API_VERSION@_la_CFLAGS) \
	 -:LDFLAGS $(libgsturidownloader_@GST_API_VERSION@_la_LDFLAGS) \
	           $(libgsturidownloader@GST_API_VERSION@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_API_VERSION@/gst/uridownloader \
	 -:HEADERS $(libgsturidownloaderinclude_HEADERS) \
         $(built_headers) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
