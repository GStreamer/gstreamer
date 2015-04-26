man_MANS = \
	gst-device-monitor-@GST_API_VERSION@.1 \
	gst-discoverer-@GST_API_VERSION@.1 \
	gst-play-@GST_API_VERSION@.1	

bin_PROGRAMS = \
	gst-device-monitor-@GST_API_VERSION@ \
	gst-discoverer-@GST_API_VERSION@ \
	gst-play-@GST_API_VERSION@

gst_device_monitor_@GST_API_VERSION@_SOURCES = gst-device-monitor.c

gst_discoverer_@GST_API_VERSION@_SOURCES = gst-discoverer.c

gst_play_@GST_API_VERSION@_SOURCES = gst-play.c gst-play-kb.c gst-play-kb.h

noinst_HEADERS = gst-play-kb.h

CLEANFILES = $(bin_SCRIPTS) $(bin_PROGRAMS)

EXTRA_DIST = $(man_MANS)


LDADD = $(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)

AM_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
