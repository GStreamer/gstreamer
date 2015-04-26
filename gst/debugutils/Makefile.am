glib_gen_prefix = __gst_debugutils
glib_gen_basename = debugutils

include $(top_srcdir)/common/gst-glib-gen.mak

built_sources = debugutils-marshal.c
built_headers = debugutils-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = debugutils-marshal.list

plugin_LTLIBRARIES = libgstdebugutilsbad.la

libgstdebugutilsbad_la_SOURCES = \
	gstdebugspy.c \
	debugutilsbad.c \
        fpsdisplaysink.c \
        gstchecksumsink.c \
	gstchecksumsink.h \
	gstchopmydata.c \
	gstchopmydata.h \
	gstcompare.c \
	gstcompare.h \
	gstdebugspy.h \
	gstwatchdog.c \
	gstwatchdog.h

nodist_libgstdebugutilsbad_la_SOURCES = $(BUILT_SOURCES)
libgstdebugutilsbad_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstdebugutilsbad_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(GST_LIBS)
libgstdebugutilsbad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdebugutilsbad_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = fpsdisplaysink.h
