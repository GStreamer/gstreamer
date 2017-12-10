noinst_PROGRAMS = fxtest 
noinst_PROGRAMS += pixbufdrop

fxtest_SOURCES = fxtest.c

fxtest_CFLAGS=\
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GL_CFLAGS) $(GTK_CFLAGS) $(X11_CFLAGS)
fxtest_LDADD=../libgstgtkhelper.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS) $(GTK_LIBS) $(X11_LIBS)

pixbufdrop_SOURCES = pixbufdrop.c

pixbufdrop_CFLAGS=\
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GL_CFLAGS) $(GTK_CFLAGS) $(X11_CFLAGS)
pixbufdrop_LDADD=../libgstgtkhelper.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS) $(GTK_LIBS) $(X11_LIBS)
