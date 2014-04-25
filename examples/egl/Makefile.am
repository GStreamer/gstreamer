noinst_PROGRAMS = 

if USE_OMX_TARGET_RPI
noinst_PROGRAMS += testegl
endif

testegl_SOURCES = testegl.c

noinst_HEADERS = cube_texture_and_coords.h

testegl_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) \
  -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_GL_LIBS) \
	-lm

testegl_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_GL_CFLAGS)

