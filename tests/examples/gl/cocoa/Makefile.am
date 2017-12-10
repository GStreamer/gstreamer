if HAVE_WINDOW_COCOA

noinst_PROGRAMS = cocoa-videooverlay

cocoa_videooverlay_SOURCES = cocoa-videooverlay.m

cocoa_videooverlay_OBJCFLAGS=-Wno-error=unused-command-line-argument \
    -fobjc-arc \
    $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
    $(GL_CFLAGS) $(GL_OBJCFLAGS)

cocoa_videooverlay_LDADD= \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS)

endif
