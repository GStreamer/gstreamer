
noinst_PROGRAMS = doublecube

doublecube_SOURCES = main.cpp

doublecube_CXXFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_CXXFLAGS) $(GL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
doublecube_LDADD=$(GST_LIBS) $(GL_LIBS)

