plugin_LTLIBRARIES = libgstmultifdsink.la

# variables used for enum/marshal generation
glib_enum_headers = gstmultifdsink.h
glib_enum_define = GST_MULTI_FD_SINK
glib_gen_prefix = gst_multi_fd_sink
glib_gen_basename = gstmultifdsink

include $(top_srcdir)/common/gst-glib-gen.mak

built_sources = gstmultifdsink-enumtypes.c gstmultifdsink-marshal.c
built_headers = gstmultifdsink-enumtypes.h gstmultifdsink-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

libgstmultifdsink_la_SOURCES = \
	gstmultifdsink.c

nodist_libgstmultifdsink_la_SOURCES = \
	$(built_sources)

libgstmultifdsink_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmultifdsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmultifdsink_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstmultifdsink_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
  gstmultifdsink.h

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gstmultifdsink-marshal.list

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmultifdsink -:SHARED libgstmultifdsink \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmultifdsink_la_SOURCES) \
	 	   $(nodist_libgstmultifdsink_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmultifdsink_la_CFLAGS) \
	 -:LDFLAGS $(libgstmultifdsink_la_LDFLAGS) \
	           $(libgstmultifdsink_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
