noinst_PROGRAMS = testegl

testegl_SOURCES = testegl.c

noinst_HEADERS = cube_texture_and_coords.h

testegl_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) \
  -lgstapp-@GST_API_VERSION@ -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_EGL_LIBS)

testegl_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_EGL_CFLAGS)

