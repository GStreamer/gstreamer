noinst_PROGRAMS = 3dvideo

noinst_HEADERS = mviewwidget.h

3dvideo_SOURCES = mviewwidget.c main.cpp

3dvideo_CXXFLAGS=$(GST_CXXFLAGS)

3dvideo_CPPFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GL_CFLAGS) $(GTK3_CFLAGS)
3dvideo_LDADD=../libgstgtkhelper.la \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la \
	$(GTK3_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) \
	$(GL_LIBS) -lgstvideo-$(GST_API_VERSION)

