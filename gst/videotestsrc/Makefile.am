plugin_LTLIBRARIES = libgstvideotestsrc.la

ORC_SOURCE=gstvideotestsrcorc
include $(top_srcdir)/common/orc.mak

libgstvideotestsrc_la_SOURCES = \
			gstvideotestsrc.c \
			videotestsrc.c
nodist_libgstvideotestsrc_la_SOURCES = $(ORC_NODIST_SOURCES)

libgstvideotestsrc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstvideotestsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideotestsrc_la_LIBADD = 	\
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS) $(LIBM)
libgstvideotestsrc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstvideotestsrc.h videotestsrc.h

EXTRA_PROGRAMS = generate_sine_table

generate_sine_table_SOURCES = generate_sine_table.c
generate_sine_table_CFLAGS = $(GST_CFLAGS)
generate_sine_table_LDADD = $(LIBM) $(GLIB_LIBS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstvideotestsrc -:SHARED libgstvideotestsrc \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvideotestsrc_la_SOURCES) \
	 	   $(nodist_libgstvideotestsrc_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvideotestsrc_la_CFLAGS) \
	 -:LDFLAGS $(libgstvideotestsrc_la_LDFLAGS) \
	           $(libgstvideotestsrc_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
