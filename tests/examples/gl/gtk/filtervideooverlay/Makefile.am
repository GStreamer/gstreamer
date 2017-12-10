noinst_PROGRAMS = filtervideooverlay

filtervideooverlay_SOURCES = main.cpp

filtervideooverlay_CXXFLAGS=\
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) \
	$(GL_CFLAGS) $(GTK_CFLAGS) $(X11_CFLAGS)
filtervideooverlay_LDADD=../libgstgtkhelper.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS) $(GTK_LIBS) $(X11_LIBS)
