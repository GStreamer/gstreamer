# please keep librfb easily extractable

noinst_LTLIBRARIES = librfb.la
plugin_LTLIBRARIES = libgstrfbsrc.la

libgstrfbsrc_la_SOURCES = gstrfbsrc.c
libgstrfbsrc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(X11_CFLAGS) -I$(srcdir)/..
libgstrfbsrc_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(X11_LIBS) librfb.la
libgstrfbsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrfbsrc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

librfb_la_SOURCES = \
	rfbbuffer.c \
	rfbdecoder.c \
	d3des.c
librfb_la_CFLAGS = $(GST_CFLAGS) $(GIO_CFLAGS) -I$(srcdir)/..
librfb_la_LIBADD = $(GST_LIBS) $(GIO_LIBS)

noinst_HEADERS = \
	rfb.h \
	rfbdecoder.h \
	rfbbuffer.h \
	rfbcontext.h \
	rfbutil.h \
	gstrfbsrc.h \
	d3des.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstrfbsrc -:SHARED libgstrfbsrc \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstrfbsrc_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstrfbsrc_la_CFLAGS) \
	 -:LDFLAGS $(libgstrfbsrc_la_LDFLAGS) \
	           $(libgstrfbsrc_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-$(GST_API_VERSION)' \
	> $@
