noinst_LTLIBRARIES = libtsmux.la

libtsmux_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS)
libtsmux_la_LIBADD = $(GST_LIBS) \
	$(top_builddir)/gst-libs/gst/mpegts/libgstmpegts-$(GST_API_VERSION).la
libtsmux_la_LDFLAGS = -module -avoid-version
libtsmux_la_SOURCES = tsmux.c tsmuxstream.c

noinst_HEADERS = tsmuxcommon.h tsmux.h tsmuxstream.h
