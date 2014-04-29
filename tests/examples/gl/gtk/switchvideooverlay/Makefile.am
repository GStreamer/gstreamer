noinst_PROGRAMS = switchvideooverlay

switchvideooverlay_SOURCES = main.cpp

switchvideooverlay_CXXFLAGS=$(GST_PLUGINS_GL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) \
	$(GL_CFLAGS) $(GTK3_CFLAGS)
switchvideooverlay_LDADD=../libgstgtkhelper.la \
	$(GTK3_LIBS) $(GST_PLUGINS_GL_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) \
	$(GL_LIBS) -lgstvideo-$(GST_API_VERSION)

