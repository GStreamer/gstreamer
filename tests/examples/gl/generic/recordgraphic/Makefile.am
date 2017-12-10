
noinst_PROGRAMS = recordgraphic

recordgraphic_SOURCES = main.cpp

recordgraphic_CXXFLAGS=\
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(GL_CFLAGS)
recordgraphic_LDADD=\
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS)

