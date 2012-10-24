plugin_LTLIBRARIES = libgsttta.la

libgsttta_la_SOURCES = gsttta.c \
	 gstttaparse.c \
	 gstttadec.c

noinst_HEADERS = gstttaparse.h gstttadec.h \
	 ttadec.h crc32.h filters.h

libgsttta_la_CFLAGS = $(GST_CFLAGS)
libgsttta_la_LIBADD = $(GST_LIBS) $(LIBM)
libgsttta_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttta_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgsttta -:SHARED libgsttta \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgsttta_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgsttta_la_CFLAGS) \
	 -:LDFLAGS $(libgsttta_la_LDFLAGS) \
	           $(libgsttta_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@