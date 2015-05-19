
noinst_PROGRAMS = cubeyuv

cubeyuv_SOURCES = main.cpp

cubeyuv_CXXFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(GL_CFLAGS)
cubeyuv_LDADD=$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(GST_LIBS) $(GL_LIBS)
