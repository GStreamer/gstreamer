plugin_LTLIBRARIES = libgstudp.la

# variables used for enum/marshal generation
glib_enum_headers = gstudp.h
glib_enum_define = GST_UDP
glib_gen_prefix = gst_udp
glib_gen_basename = gstudp

include $(top_srcdir)/common/gst-glib-gen.mak

built_sources = gstudp-enumtypes.c gstudp-marshal.c
built_headers = gstudp-enumtypes.h gstudp-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

libgstudp_la_SOURCES = gstudp.c gstudpsrc.c gstudpsink.c gstmultiudpsink.c gstdynudpsink.c gstudpnetutils.c

# adding -D_GNU_SOURCE to get non-POSIX extensions like EAI_ADDRFAMILY
# with glibc >= 2.8 when including netdb.h (see glibc sources bug 6452)
libgstudp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) -D_GNU_SOURCE
libgstudp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstnetbuffer-@GST_MAJORMINOR@ \
			$(GST_BASE_LIBS) $(WIN32_LIBS)
libgstudp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstudp_la_LIBTOOLFLAGS = --tag=disable-static

nodist_libgstudp_la_SOURCES = \
        $(built_sources)

noinst_HEADERS = gstudpsink.h gstudpsrc.h gstudp.h gstmultiudpsink.h gstdynudpsink.h gstudpnetutils.h

EXTRA_DIST = README gstudp-marshal.list

CLEANFILES = $(BUILT_SOURCES)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstudp -:SHARED libgstudp \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstudp_la_SOURCES) \
	 	   $(nodist_libgstudp_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstudp_la_CFLAGS) \
	 -:LDFLAGS $(libgstudp_la_LDFLAGS) \
	           $(libgstudp_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
