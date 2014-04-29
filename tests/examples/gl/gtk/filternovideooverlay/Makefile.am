noinst_PROGRAMS = filternovideooverlay

filternovideooverlay_SOURCES = main.cpp

filternovideooverlay_CXXFLAGS= \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CXXFLAGS) \
	$(GL_CFLAGS) \
	$(GTK3_CFLAGS)

filternovideooverlay_LDADD= \
	$(GTK3_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS)  \
	$(GL_LIBS)

