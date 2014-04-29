noinst_PROGRAMS = fxtest 
noinst_PROGRAMS += pixbufdrop

fxtest_SOURCES = fxtest.c

fxtest_CFLAGS=$(GST_PLUGINS_GL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GL_CFLAGS) $(GTK3_CFLAGS)
fxtest_LDADD=../libgstgtkhelper.la \
	$(GTK3_LIBS) $(GST_PLUGINS_GL_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS)  \
	$(GL_LIBS) -lgstvideo-$(GST_API_VERSION)

pixbufdrop_SOURCES = pixbufdrop.c

pixbufdrop_CFLAGS=$(GST_PLUGINS_GL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GL_CFLAGS) $(GTK3_CFLAGS)
pixbufdrop_LDADD=../libgstgtkhelper.la \
	$(GTK3_LIBS) $(GST_PLUGINS_GL_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) \
	$(GL_LIBS) -lgstvideo-$(GST_API_VERSION)
