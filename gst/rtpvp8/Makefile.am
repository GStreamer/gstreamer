plugin_LTLIBRARIES = libgstrtpvp8.la

libgstrtpvp8_la_SOURCES = gstrtpvp8.c \
  gstrtpvp8depay.c \
  gstrtpvp8pay.c \
  dboolhuff.c

noinst_HEADERS = gstrtpvp8depay.h \
  gstrtpvp8pay.h \
  dboolhuff.h

libgstrtpvp8_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtpvp8_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstrtpvp8_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtpvp8_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstrtpvp8 -:SHARED libgstrtpvp8 \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstrtpvp8_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstrtpvp8_la_CFLAGS) \
	 -:LDFLAGS $(libgstrtpvp8_la_LDFLAGS) \
	           $(libgstrtpvp8_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
