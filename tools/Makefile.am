man_MANS = \
        gst-discoverer-@GST_API_VERSION@.1

bin_PROGRAMS = \
	gst-discoverer-@GST_API_VERSION@

gst_discoverer_@GST_API_VERSION@_SOURCES = gst-discoverer.c

CLEANFILES = $(bin_SCRIPTS) $(bin_PROGRAMS)

EXTRA_DIST = gst-discoverer-1.0.1


LDADD = $(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS)

AM_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)

Android.mk: Makefile.am
	androgenizer -:PROJECT gst-discoverer-@GST_API_VERSION@ \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:EXECUTABLE gst-discoverer-@GST_API_VERSION@ -:TAGS eng debug \
	 -:SOURCES $(gst_discoverer_@GST_API_VERSION@_SOURCES) \
	 -:CFLAGS $(DEFS) $(AM_CFLAGS) \
	 -:LDFLAGS -lgstpbutils-@GST_API_VERSION@ -lgstvideo-@GST_API_VERSION@ \
	 	   $(GST_BASE_LIBS) $(GST_LIBS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
