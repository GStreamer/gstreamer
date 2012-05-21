noinst_PROGRAMS = playback-test

LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GTK_LIBS) $(LIBM)

if HAVE_GTK_QUARTZ
  AM_CPPFLAGS = '-xobjective-c'
endif

AM_CFLAGS = -I$(top_builddir)/gst-libs $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GTK_CFLAGS)
