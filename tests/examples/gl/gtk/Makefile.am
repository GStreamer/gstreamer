
SUBDIRS = . filternovideooverlay filtervideooverlay fxtest switchvideooverlay 3dvideo

noinst_LTLIBRARIES = libgstgtkhelper.la

libgstgtkhelper_la_SOURCES = gstgtk.c gstgtk.h
libgstgtkhelper_la_CFLAGS = -I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GTK3_CFLAGS)

if HAVE_WINDOW_COCOA
libgstgtkhelper_la_CFLAGS += -x objective-c
endif

