
noinst_PROGRAMS = cubeyuv

cubeyuv_SOURCES = main.cpp

cubeyuv_CXXFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_CXXFLAGS) $(GL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
cubeyuv_LDADD=$(GST_LIBS) $(GL_LIBS)

