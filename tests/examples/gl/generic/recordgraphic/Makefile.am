
noinst_PROGRAMS = recordgraphic

recordgraphic_SOURCES = main.cpp

recordgraphic_CXXFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_CXXFLAGS) $(GL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
recordgraphic_LDADD=$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ $(GST_LIBS) $(GL_LIBS)

