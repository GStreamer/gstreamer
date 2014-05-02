if HAVE_WINDOW_COCOA

noinst_PROGRAMS = cocoa-videooverlay

cocoa_videooverlay_SOURCES = cocoa-videooverlay.m

cocoa_videooverlay_OBJCFLAGS=$(GST_PLUGINS_GL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GL_CFLAGS) ${GL_OBJCFLAGS}
cocoa_videooverlay_LDADD=$(GST_PLUGINS_GL_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) \
	$(GL_LIBS) -lgstvideo-$(GST_API_VERSION)

cocoa_videooverlay_LIBTOOLFLAGS = --tag=OBJC

endif
