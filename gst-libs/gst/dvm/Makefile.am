lib_LTLIBRARIES = libgstdvm-@GST_MAJORMINOR@.la
libgstdvmincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/dvm

libgstdvminclude_HEADERS = \
	gst-dvm.h

libgstdvm_@GST_MAJORMINOR@_la_SOURCES = \
	gst-dvm.c

libgstdvm_@GST_MAJORMINOR@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_CFLAGS)
libgstdvm_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) 
libgstdvm_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

Android.mk:  $(BUILT_SOURCES) Makefile.am
	androgenizer -:PROJECT libgstdvm -:STATIC libgstdvm-@GST_MAJORMINOR@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstdvm_@GST_MAJORMINOR@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(libgstdvm_@GST_MAJORMINOR@_la_CFLAGS) \
	 -:LDFLAGS $(libgstdvm_@GST_MAJORMINOR@_la_LDFLAGS) \
	           $(libgstdvm_@GST_MAJORMINOR@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_MAJORMINOR@/gst/dvm \
	 -:HEADERS $(libgstdvminclude_HEADERS)
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
