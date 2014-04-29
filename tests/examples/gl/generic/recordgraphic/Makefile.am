
noinst_PROGRAMS = recordgraphic

recordgraphic_SOURCES = main.cpp

recordgraphic_CXXFLAGS=$(GST_PLUGINS_GL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(GL_CFLAGS)
recordgraphic_LDADD=$(GST_PLUGINS_GL_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ $(GST_LIBS) $(GL_LIBS)

