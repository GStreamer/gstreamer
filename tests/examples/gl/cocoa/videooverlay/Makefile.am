if HAVE_WINDOW_COCOA

noinst_PROGRAMS = videooverlay

videooverlay_SOURCES = main.m

videooverlay_OBJCFLAGS=$(GST_PLUGINS_GL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GL_CFLAGS) -I/usr/local/include/gstreamer-${GST_API_VERSION} ${GL_OBJCFLAGS}
videooverlay_LDADD=$(GST_PLUGINS_GL_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) \
	$(GL_LIBS) -lgstvideo-$(GST_API_VERSION)

videooverlay_LIBTOOLFLAGS = --tag=OBJC

endif
