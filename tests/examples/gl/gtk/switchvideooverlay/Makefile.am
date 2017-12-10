noinst_PROGRAMS = switchvideooverlay

switchvideooverlay_SOURCES = main.cpp

switchvideooverlay_CXXFLAGS=\
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) \
	$(GL_CFLAGS) $(GTK_CFLAGS) $(X_CFLAGS)
switchvideooverlay_LDADD=../libgstgtkhelper.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS) $(GTK_LIBS) $(X_LIBS)

