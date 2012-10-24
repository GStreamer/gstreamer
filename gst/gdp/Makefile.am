plugin_LTLIBRARIES = libgstgdp.la

libgstgdp_la_SOURCES = \
	dataprotocol.c \
	gstgdp.c \
	gstgdppay.c \
	gstgdpdepay.c

libgstgdp_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstgdp_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstgdp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgdp_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	dataprotocol.h \
	dp-private.h   \
	gstgdppay.h    \
	gstgdpdepay.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstgdp -:SHARED libgstgdp \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstgdp_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstgdp_la_CFLAGS) \
	 -:LDFLAGS $(libgstgdp_la_LDFLAGS) \
	           $(libgstgdp_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
