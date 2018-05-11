noinst_PROGRAMS = filternovideooverlay

filternovideooverlay_SOURCES = main.cpp

filternovideooverlay_CXXFLAGS= \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CXXFLAGS) \
	$(GL_CFLAGS) \
	$(GTK_CFLAGS) -Wno-error=parentheses

filternovideooverlay_LDADD= \
	$(GST_LIBS)  \
	$(GL_LIBS) \
	$(GTK_LIBS)

