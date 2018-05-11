noinst_PROGRAMS = 3dvideo

noinst_HEADERS = mviewwidget.h

3dvideo_SOURCES = mviewwidget.c main.cpp

3dvideo_CXXFLAGS=$(GST_CXXFLAGS) -Wno-error=parentheses
3dvideo_CFLAGS=$(GST_CFLAGS)

3dvideo_CPPFLAGS=\
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GL_CFLAGS) $(GTK_CFLAGS)
3dvideo_LDADD=../libgstgtkhelper.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS) $(GTK_LIBS)
