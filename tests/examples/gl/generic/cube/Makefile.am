
noinst_PROGRAMS = cube

cube_SOURCES = main.cpp

cube_CXXFLAGS=\
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(GL_CFLAGS)
cube_LDADD=\
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS)

