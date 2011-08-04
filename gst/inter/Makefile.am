plugin_LTLIBRARIES = libgstinter.la

noinst_PROGRAMS = gstintertest

libgstinter_la_SOURCES = \
	gstinteraudiosink.c \
	gstinteraudiosrc.c \
	gstintervideosink.c \
	gstintervideosrc.c \
	gstinter.c \
	gstintersurface.c

noinst_HEADERS = \
	gstinteraudiosink.h \
	gstinteraudiosrc.h \
	gstintervideosink.h \
	gstintervideosrc.h \
	gstintersurface.h

libgstinter_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS)

libgstinter_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ -lgstaudio-@GST_MAJORMINOR@ \
	$(LIBM)

libgstinter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstinter_la_LIBTOOLFLAGS = --tag=disable-static

gstintertest_SOURCES = \
	gstintertest.c

gstintertest_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)

gstintertest_LDADD = \
	$(GST_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	$(LIBM)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstinter -:SHARED libgstinter \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstinter_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstinter_la_CFLAGS) \
	 -:LDFLAGS $(libgstinter_la_LDFLAGS) \
	           $(libgstinter_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
