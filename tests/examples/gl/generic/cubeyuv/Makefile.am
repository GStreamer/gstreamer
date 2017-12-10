
noinst_PROGRAMS = cubeyuv

cubeyuv_SOURCES = main.cpp

cubeyuv_CXXFLAGS=\
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(GL_CFLAGS)
cubeyuv_LDADD=\
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS)
