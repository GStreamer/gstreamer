noinst_PROGRAMS = filtervideooverlay

filtervideooverlay_SOURCES = main.cpp

filtervideooverlay_CXXFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
    $(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) \
	$(GL_CFLAGS) $(GTK3_CFLAGS)
filtervideooverlay_LDADD=../libgstgtkhelper.la \
	$(GTK3_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) \
	$(GL_LIBS) -lgstvideo-$(GST_API_VERSION)

