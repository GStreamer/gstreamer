noinst_PROGRAMS = test-cleanup test-reuse

AM_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir) -I$(srcdir)
AM_LDFLAGS = \
	$(GST_LIBS) \
	$(top_builddir)/gst/rtsp-server/libgstrtspserver-@GST_API_VERSION@.la

SUBDIRS = check

DIST_SUBDIRS = check
